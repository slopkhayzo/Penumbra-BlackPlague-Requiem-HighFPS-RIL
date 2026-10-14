#include "world_runtime.h"

#include "guarded_memory.h"
#include "target_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::world {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }


} // namespace

void Runtime::Configure(void* const setMatrix) noexcept {
    (void)EndRender();
    setMatrix_ = setMatrix;
    slots_ = {};
    renderActive_ = false;
}

void Runtime::Reset() noexcept {
    (void)EndRender();
    slots_ = {};
    renderActive_ = false;
}

Runtime::Slot* Runtime::FindOrCreate(
    void* const node, const std::uint64_t pendingSerial) noexcept {
    Slot* empty = nullptr;
    Slot* oldest = nullptr;
    for (Slot& slot : slots_) {
        if (slot.occupied && slot.node == node) {
            return &slot;
        }
        if (!slot.occupied && empty == nullptr) {
            empty = &slot;
        }
        // A node that already moved in the pending tick is live; recycling
        // it would silently drop its history every tick once the table fills.
        if (slot.occupied && slot.lastSeenSerial < pendingSerial &&
            (oldest == nullptr || slot.lastSeenSerial < oldest->lastSeenSerial)) {
            oldest = &slot;
        }
    }
    Slot* selected = empty != nullptr ? empty : oldest;
    if (selected == nullptr) {
        return nullptr;
    }
    *selected = {};
    selected->node = node;
    selected->occupied = true;
    return selected;
}

bool Runtime::IsSameObject(const Slot& slot) const noexcept {
    const void* vtable = nullptr;
    return memory::TryReadAt(slot.node, 0, vtable) && vtable == slot.vtable;
}

bool Runtime::ReadLocalMatrix(void* const node,
                              transform::Matrix& matrix) const noexcept {
    if (node == nullptr) {
        return false;
    }
    return memory::TryReadAt(node, Fields().nodeLocalMatrix, matrix) &&
           transform::IsRigid(matrix);
}

bool Runtime::SetLocalMatrix(void* const node,
                             const transform::Matrix& matrix) const noexcept {
    if (node == nullptr || setMatrix_ == nullptr ||
        !transform::IsRigid(matrix)) {
        return false;
    }
    using SetMatrixFn =
        void(__thiscall*)(void*, const transform::Matrix&, bool);
    const auto setMatrix = reinterpret_cast<SetMatrixFn>(setMatrix_);
    setMatrix(node, matrix, true);
    transform::Matrix applied{};
    return ReadLocalMatrix(node, applied) &&
           transform::NearlyEqual(applied, matrix, 0.0F);
}

NotifyResult Runtime::NotifyNodeMoved(
    void* const node, const transform::Matrix& matrix,
    const std::uint64_t pendingSerial) noexcept {
    if (node == nullptr || pendingSerial == 0 || !transform::IsRigid(matrix)) {
        return NotifyResult::invalid;
    }
    Slot* const slot = FindOrCreate(node, pendingSerial);
    if (slot == nullptr) {
        return NotifyResult::capacityExhausted;
    }
    if (slot->stagedSerial != 0 && slot->stagedSerial != pendingSerial) {
        slot->history.Reset();
        slot->hasAuthoritative = false;
    }
    // Remember which object this is. The engine destroys nodes without
    // telling anyone (a picked-up item takes its body node with it), and the
    // destructor or the heap rewrites the first word.
    const void* vtable = nullptr;
    (void)memory::TryReadAt(node, 0, vtable);
    if (slot->vtable != vtable) {
        slot->history.Reset();
        slot->hasAuthoritative = false;
        slot->vtable = vtable;
    }
    slot->staged = matrix;
    slot->stagedSerial = pendingSerial;
    slot->lastSeenSerial = pendingSerial;
    return NotifyResult::staged;
}

BatchResult Runtime::CommitCompletedTick(
    const std::uint64_t tickSerial,
    const std::uint64_t clockGeneration) noexcept {
    BatchResult result{};
    for (Slot& slot : slots_) {
        if (!slot.occupied) {
            continue;
        }
        if (slot.lastSeenSerial != 0 &&
            tickSerial > slot.lastSeenSerial + kStaleTicks) {
            slot = {};
            continue;
        }
        if (slot.stagedSerial != tickSerial) {
            continue;
        }
        const transform::PublishKind published =
            slot.history.Publish(tickSerial, clockGeneration, slot.staged);
        switch (published) {
        case transform::PublishKind::appended:
        case transform::PublishKind::unchanged:
            ++result.captured;
            break;
        case transform::PublishKind::invalid:
            ++result.invalid;
            break;
        case transform::PublishKind::resetGeneration:
        case transform::PublishKind::resetNonMonotonic:
        case transform::PublishKind::resetSameTickMutation:
        case transform::PublishKind::resetMissingTick:
        case transform::PublishKind::resetDiscontinuity:
            ++result.reset;
            break;
        }
        slot.authoritative = slot.staged;
        slot.hasAuthoritative = true;
        slot.stagedSerial = 0;
    }
    return result;
}

BatchResult Runtime::BeginRender(const clock::Sample& clockSample) noexcept {
    BatchResult result{};
    if (renderActive_) {
        ++result.applyFailed;
        return result;
    }
    renderActive_ = true;
    for (Slot& slot : slots_) {
        if (!slot.occupied || !slot.hasAuthoritative) {
            continue;
        }
        const transform::SampleResult sampled =
            slot.history.Evaluate(clockSample);
        if (sampled.kind != transform::SampleKind::applied) {
            if (sampled.kind == transform::SampleKind::snapshotsMissing) {
                ++result.pairMiss;
            }
            continue;
        }
        // A destroyed node keeps its last matrix in freed memory, so the
        // matrix comparison alone cannot tell it from a live one. Calling
        // SetMatrix on it walks freed child lists.
        if (!IsSameObject(slot)) {
            slot = {};
            ++result.invalid;
            continue;
        }
        if (!ReadLocalMatrix(slot.node, slot.restore) ||
            !transform::NearlyEqual(slot.restore, slot.authoritative,
                                    1.0e-4F)) {
            slot.history.Reset();
            slot.hasAuthoritative = false;
            ++result.invalid;
            continue;
        }
        if (!SetLocalMatrix(slot.node, sampled.matrix)) {
            (void)SetLocalMatrix(slot.node, slot.restore);
            ++result.applyFailed;
            continue;
        }
        slot.overridden = true;
        ++result.ready;
    }
    return result;
}

BatchResult Runtime::EndRender() noexcept {
    BatchResult result{};
    if (!renderActive_) {
        return result;
    }
    for (Slot& slot : slots_) {
        if (!slot.occupied || !slot.overridden) {
            continue;
        }
        if (!SetLocalMatrix(slot.node, slot.restore)) {
            ++result.restoreFailed;
        }
        slot.overridden = false;
    }
    renderActive_ = false;
    return result;
}

} // namespace pbpfps::world
