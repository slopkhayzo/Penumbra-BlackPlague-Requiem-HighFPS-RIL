#include "entity_root_runtime.h"

#include "guarded_memory.h"
#include "target_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::entity_root {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }


} // namespace

void Runtime::Configure(void* const setMatrix,
                        void* const meshGetModelMatrixTarget) noexcept {
    (void)EndRender();
    setMatrix_ = setMatrix;
    meshGetModelMatrixTarget_ = meshGetModelMatrixTarget;
    slots_ = {};
    renderActive_ = false;
}

void Runtime::Reset() noexcept {
    (void)EndRender();
    slots_ = {};
    renderActive_ = false;
}

Runtime::Slot* Runtime::FindOrCreate(void* const entity) noexcept {
    Slot* empty = nullptr;
    for (Slot& slot : slots_) {
        if (slot.occupied && slot.entity == entity) {
            return &slot;
        }
        if (!slot.occupied && empty == nullptr) {
            empty = &slot;
        }
    }
    if (empty == nullptr) {
        return nullptr;
    }
    *empty = {};
    empty->entity = entity;
    empty->occupied = true;
    return empty;
}

bool Runtime::IsExpectedMeshEntity(void* const entity) const noexcept {
    return memory::HasVtableEntry(entity, Fields().meshGetModelMatrixVtableIndex,
                                  meshGetModelMatrixTarget_);
}

bool Runtime::ReadLocalMatrix(void* const entity,
                              transform::Matrix& matrix) const noexcept {
    if (!IsExpectedMeshEntity(entity)) {
        return false;
    }
    return memory::TryReadAt(entity, Fields().entityLocalMatrix, matrix) &&
           transform::IsRigid(matrix);
}

bool Runtime::SetLocalMatrix(
    void* const entity, const transform::Matrix& matrix) const noexcept {
    if (!IsExpectedMeshEntity(entity) || setMatrix_ == nullptr ||
        !transform::IsRigid(matrix)) {
        return false;
    }
    using SetMatrixFn =
        void(__thiscall*)(void*, const transform::Matrix&);
    reinterpret_cast<SetMatrixFn>(setMatrix_)(entity, matrix);
    transform::Matrix applied{};
    return ReadLocalMatrix(entity, applied) &&
           transform::NearlyEqual(applied, matrix, 0.0F);
}

NotifyResult Runtime::NotifyMoved(
    void* const entity, const transform::Matrix& matrix,
    const std::uint64_t pendingSerial) noexcept {
    if (entity == nullptr || pendingSerial == 0 ||
        !transform::IsRigid(matrix)) {
        return NotifyResult::invalid;
    }
    if (!IsExpectedMeshEntity(entity)) {
        return NotifyResult::ignored;
    }
    Slot* const slot = FindOrCreate(entity);
    if (slot == nullptr) {
        return NotifyResult::capacityExhausted;
    }
    if (slot->stagedSerial != 0 && slot->stagedSerial != pendingSerial) {
        slot->history.Reset();
        slot->hasAuthoritative = false;
    }
    slot->staged = matrix;
    slot->stagedSerial = pendingSerial;
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
        if (slot.stagedSerial != tickSerial) {
            slot = {};
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

BatchResult Runtime::BeginRender(
    const clock::Sample& clockSample) noexcept {
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
        if (!ReadLocalMatrix(slot.entity, slot.restore) ||
            !transform::NearlyEqual(slot.restore, slot.authoritative,
                                    1.0e-4F)) {
            slot = {};
            ++result.invalid;
            continue;
        }
        if (!SetLocalMatrix(slot.entity, sampled.matrix)) {
            (void)SetLocalMatrix(slot.entity, slot.restore);
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
    for (std::size_t index = slots_.size(); index > 0; --index) {
        Slot& slot = slots_[index - 1];
        if (!slot.occupied || !slot.overridden) {
            continue;
        }
        if (!SetLocalMatrix(slot.entity, slot.restore)) {
            ++result.restoreFailed;
        }
        slot.overridden = false;
    }
    renderActive_ = false;
    return result;
}

} // namespace pbpfps::entity_root
