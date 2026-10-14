#include "pose_runtime.h"

#include "guarded_memory.h"
#include "target_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::pose {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }


template <typename T>
bool ReadAt(const void* base, const std::size_t offset, T& value) noexcept {
    return memory::TryReadAt(base, static_cast<std::ptrdiff_t>(offset),
                             value);
}

struct VectorView {
    transform::Matrix* begin = nullptr;
    transform::Matrix* end = nullptr;
    transform::Matrix* capacity = nullptr;
    std::size_t count = 0;
};

bool ReadBoneVector(void* mesh, VectorView& view) noexcept {
    if (!ReadAt(mesh, Fields().meshBoneMatricesBegin, view.begin) ||
        !ReadAt(mesh, Fields().meshBoneMatricesEnd, view.end) ||
        !ReadAt(mesh, Fields().meshBoneMatricesCapacity, view.capacity) ||
        view.begin == nullptr) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(view.begin);
    const auto end = reinterpret_cast<std::uintptr_t>(view.end);
    const auto capacity = reinterpret_cast<std::uintptr_t>(view.capacity);
    if (end < begin || capacity < end) {
        return false;
    }
    const std::size_t bytes = end - begin;
    const std::size_t capacityBytes = capacity - begin;
    if (bytes % sizeof(transform::Matrix) != 0 ||
        capacityBytes % sizeof(transform::Matrix) != 0) {
        return false;
    }
    view.count = bytes / sizeof(transform::Matrix);
    return view.count != 0 && view.count <= kMaximumBones;
}

bool HasVtableTarget(void* object, const std::size_t index,
                     void* expected) noexcept {
    return memory::HasVtableEntry(object, index, expected);
}

} // namespace

void Runtime::Configure(const RuntimeFunctions& functions,
                        const RuntimeFeatures& features) noexcept {
    Reset();
    functions_ = functions;
    features_ = features;
}

void Runtime::Clear(Slot& slot) noexcept { slot = {}; }

void Runtime::Reset() noexcept {
    (void)EndRender();
    for (Slot& slot : slots_) {
        Clear(slot);
    }
    handsMarkers_ = {};
    tickSerial_ = 0;
    clockGeneration_ = 0;
    epoch_ = 0;
    collecting_ = false;
    renderActive_ = false;
}

void Runtime::MarkHandsMesh(void* const mesh,
                            const std::uint64_t pendingSerial) noexcept {
    if (mesh == nullptr || pendingSerial == 0) {
        return;
    }
    HandsMarker* empty = nullptr;
    for (HandsMarker& marker : handsMarkers_) {
        if (marker.mesh == mesh) {
            marker.serial = pendingSerial;
            return;
        }
        if (marker.mesh == nullptr && empty == nullptr) {
            empty = &marker;
        }
    }
    if (empty != nullptr) {
        *empty = {mesh, pendingSerial};
        return;
    }
    HandsMarker* oldest = &handsMarkers_[0];
    for (HandsMarker& marker : handsMarkers_) {
        if (marker.serial < oldest->serial) {
            oldest = &marker;
        }
    }
    *oldest = {mesh, pendingSerial};
}

void Runtime::BeginCollection(const std::uint64_t tickSerial,
                              const std::uint64_t clockGeneration) noexcept {
    tickSerial_ = tickSerial;
    clockGeneration_ = clockGeneration;
    ++epoch_;
    if (epoch_ == 0) {
        epoch_ = 1;
        for (Slot& slot : slots_) {
            slot.observedEpoch = 0;
        }
    }
    for (Slot& slot : slots_) {
        slot.subMeshes = {};
        slot.subMeshCount = 0;
    }
    collecting_ = true;
}

void Runtime::EndCollection() noexcept { collecting_ = false; }

Runtime::Slot* Runtime::Find(void* const mesh) noexcept {
    for (Slot& slot : slots_) {
        if (slot.occupied && slot.mesh == mesh) {
            return &slot;
        }
    }
    return nullptr;
}

Runtime::Slot* Runtime::FindOrRecycle(void* const mesh) noexcept {
    if (Slot* const found = Find(mesh); found != nullptr) {
        return found;
    }
    Slot* candidate = nullptr;
    for (Slot& slot : slots_) {
        if (!slot.occupied) {
            candidate = &slot;
            break;
        }
        if (slot.observedEpoch != epoch_ &&
            (candidate == nullptr ||
             slot.observedEpoch < candidate->observedEpoch)) {
            candidate = &slot;
        }
    }
    if (candidate == nullptr) {
        return nullptr;
    }
    Clear(*candidate);
    candidate->mesh = mesh;
    candidate->occupied = true;
    return candidate;
}

bool Runtime::IsHandsMesh(void* const mesh) const noexcept {
    for (const HandsMarker& marker : handsMarkers_) {
        if (marker.mesh == mesh && marker.serial == tickSerial_) {
            return true;
        }
    }
    return false;
}

bool Runtime::IsKindEnabled(const Kind kind) const noexcept {
    switch (kind) {
    case Kind::hands:
        return features_.hands;
    case Kind::world:
        return features_.world;
    case Kind::ragdoll:
        return features_.ragdoll;
    }
    return false;
}

bool Runtime::ValidateMesh(void* const mesh) const noexcept {
    return HasVtableTarget(mesh, Fields().updateGraphicsVtableIndex,
                           functions_.meshUpdateGraphicsTarget);
}

ObserveMeshResult Runtime::ObserveMesh(void* const mesh) noexcept {
    if (!collecting_ || tickSerial_ == 0) {
        return ObserveMeshResult::ignored;
    }
    if (!ValidateMesh(mesh)) {
        return ObserveMeshResult::invalid;
    }
    std::uint8_t skeletonPhysics = 0;
    if (!ReadAt(mesh, Fields().meshSkeletonPhysicsActive, skeletonPhysics) ||
        skeletonPhysics > 1) {
        return ObserveMeshResult::invalid;
    }
    const Kind kind = IsHandsMesh(mesh)
                          ? Kind::hands
                          : (skeletonPhysics != 0 ? Kind::ragdoll
                                                  : Kind::world);
    if (!IsKindEnabled(kind)) {
        return ObserveMeshResult::ignored;
    }

    VectorView vector{};
    if (!ReadBoneVector(mesh, vector)) {
        // Most visible mesh entities are not skeletal; an empty palette is an
        // expected filter result rather than a malformed target.
        return ObserveMeshResult::ignored;
    }
    Slot* const slot = FindOrRecycle(mesh);
    if (slot == nullptr) {
        return ObserveMeshResult::capacityExhausted;
    }
    if (slot->hasAuthoritative && slot->kind != kind) {
        slot->history.Reset();
        slot->lastCapturedSerial = 0;
        slot->hasAuthoritative = false;
        slot->changedOnLastCapture = false;
    }
    slot->observedEpoch = epoch_;
    slot->kind = kind;

    const bool identityChanged =
        slot->identityBuffer != vector.begin ||
        slot->identityBoneCount != vector.count;
    if (identityChanged) {
        slot->history.Reset();
        slot->identityBuffer = vector.begin;
        slot->identityBoneCount = vector.count;
        slot->lastCapturedSerial = 0;
        slot->hasAuthoritative = false;
        slot->changedOnLastCapture = false;
    }

    Pose current{};
    current.boneCount = vector.count;
    if (!memory::TryCopy(current.bones.data(), vector.begin,
                         vector.count * sizeof(transform::Matrix)) ||
        !IsValid(current)) {
        slot->history.Reset();
        slot->hasAuthoritative = false;
        return ObserveMeshResult::invalid;
    }
    if (slot->lastCapturedSerial == tickSerial_ &&
        slot->hasAuthoritative &&
        NearlyEqual(slot->authoritative, current)) {
        return ObserveMeshResult::unchanged;
    }

    const bool changed = slot->hasAuthoritative &&
                         !NearlyEqual(slot->authoritative, current);

    const PublishKind published =
        slot->history.Publish(tickSerial_, clockGeneration_, current);
    slot->authoritative = current;
    slot->lastCapturedSerial = tickSerial_;
    slot->hasAuthoritative = published != PublishKind::invalid;
    slot->changedOnLastCapture = changed;
    switch (published) {
    case PublishKind::appended:
        return ObserveMeshResult::captured;
    case PublishKind::unchanged:
        return ObserveMeshResult::unchanged;
    case PublishKind::invalid:
        return ObserveMeshResult::invalid;
    case PublishKind::resetGeneration:
    case PublishKind::resetNonMonotonic:
    case PublishKind::resetSameTickMutation:
    case PublishKind::resetBoneCount:
    case PublishKind::resetMissingTick:
    case PublishKind::resetDiscontinuity:
        return ObserveMeshResult::historyReset;
    }
    return ObserveMeshResult::invalid;
}

ObserveSubMeshResult Runtime::ObserveSubMesh(
    void* const subMesh, void* const camera, const float frameTime,
    void* const renderList) noexcept {
    if (!collecting_) {
        return ObserveSubMeshResult::ignored;
    }
    if (subMesh == nullptr ||
        !HasVtableTarget(subMesh, Fields().updateGraphicsVtableIndex,
                         functions_.subMeshUpdateGraphicsTarget)) {
        return ObserveSubMeshResult::invalid;
    }
    void* owner = nullptr;
    void* dynamicVertexBuffer = nullptr;
    if (!ReadAt(subMesh, Fields().subMeshOwner, owner) || owner == nullptr ||
        !ReadAt(subMesh, Fields().subMeshDynamicVertexBuffer,
                dynamicVertexBuffer)) {
        return ObserveSubMeshResult::invalid;
    }
    if (dynamicVertexBuffer == nullptr) {
        return ObserveSubMeshResult::ignored;
    }
    Slot* const slot = Find(owner);
    if (slot == nullptr || slot->observedEpoch != epoch_ ||
        !IsKindEnabled(slot->kind)) {
        return ObserveSubMeshResult::ignored;
    }
    for (std::size_t index = 0; index < slot->subMeshCount; ++index) {
        if (slot->subMeshes[index].subMesh == subMesh) {
            return ObserveSubMeshResult::duplicate;
        }
    }
    if (slot->subMeshCount == slot->subMeshes.size()) {
        return ObserveSubMeshResult::capacityExhausted;
    }
    slot->subMeshes[slot->subMeshCount++] =
        {subMesh, camera, renderList, frameTime};
    return ObserveSubMeshResult::added;
}

bool Runtime::ValidateSubMesh(const Slot& slot,
                              const SubMeshCall& call) const noexcept {
    if (!HasVtableTarget(call.subMesh, Fields().updateGraphicsVtableIndex,
                         functions_.subMeshUpdateGraphicsTarget)) {
        return false;
    }
    void* owner = nullptr;
    void* dynamicVertexBuffer = nullptr;
    std::uint8_t updated = 0;
    return ReadAt(call.subMesh, Fields().subMeshOwner, owner) &&
           owner == slot.mesh &&
           ReadAt(call.subMesh, Fields().subMeshDynamicVertexBuffer,
                  dynamicVertexBuffer) &&
           dynamicVertexBuffer != nullptr &&
           ReadAt(call.subMesh, Fields().subMeshGraphicsUpdated, updated) &&
           updated <= 1;
}

bool Runtime::Regenerate(Slot& slot, std::uint32_t& calls,
                         std::uint32_t& sleepingCalls) const noexcept {
    if (functions_.subMeshUpdateGraphics == nullptr) {
        return false;
    }
    std::uint8_t sleeping = 0;
    if (!ReadAt(slot.mesh, Fields().meshSkeletonPhysicsSleeping, sleeping) ||
        sleeping > 1) {
        return false;
    }
    for (std::size_t index = 0; index < slot.subMeshCount; ++index) {
        if (!ValidateSubMesh(slot, slot.subMeshes[index])) {
            return false;
        }
    }

    using UpdateGraphicsFn =
        void(__thiscall*)(void*, void*, float, void*);
    const auto update = reinterpret_cast<UpdateGraphicsFn>(
        functions_.subMeshUpdateGraphics);
    for (std::size_t index = 0; index < slot.subMeshCount; ++index) {
        const SubMeshCall& call = slot.subMeshes[index];
        auto* updated = static_cast<std::uint8_t*>(call.subMesh) +
                        Fields().subMeshGraphicsUpdated;
        std::uint8_t savedUpdated = 0;
        if (sleeping != 0 &&
            (!memory::TryRead(updated, savedUpdated) ||
             !memory::TryWrite(updated, std::uint8_t{0}))) {
            return false;
        }
        update(call.subMesh, call.camera, call.frameTime, call.renderList);
        if (sleeping != 0 && !memory::TryWrite(updated, savedUpdated)) {
            return false;
        }
        if (sleeping != 0) {
            ++sleepingCalls;
        }
        ++calls;
    }
    return true;
}

BatchResult Runtime::BeginRender(
    const clock::Sample& clockSample) noexcept {
    BatchResult result{};
    if (renderActive_) {
        ++result.invalid;
        return result;
    }
    renderActive_ = true;
    for (Slot& slot : slots_) {
        if (!slot.occupied || !slot.hasAuthoritative ||
            slot.observedEpoch != epoch_ || slot.subMeshCount == 0) {
            continue;
        }
        if (slot.kind == Kind::ragdoll && !slot.changedOnLastCapture) {
            ++result.stationarySkipped;
            continue;
        }
        ++result.targets;
        const SampleResult sampled = slot.history.Evaluate(clockSample);
        switch (sampled.kind) {
        case SampleKind::clockUnavailable:
        case SampleKind::generationMismatch:
            ++result.clockWait;
            continue;
        case SampleKind::snapshotsMissing:
            ++result.pairMissing;
            continue;
        case SampleKind::invalid:
            ++result.invalid;
            continue;
        case SampleKind::applied:
            break;
        }

        VectorView vector{};
        if (!ValidateMesh(slot.mesh) || !ReadBoneVector(slot.mesh, vector) ||
            vector.begin != slot.identityBuffer ||
            vector.count != slot.identityBoneCount ||
            sampled.pose.boneCount != vector.count) {
            slot.history.Reset();
            slot.hasAuthoritative = false;
            ++result.invalid;
            continue;
        }
        Pose live{};
        live.boneCount = vector.count;
        if (!memory::TryCopy(live.bones.data(), vector.begin,
                             vector.count * sizeof(transform::Matrix)) ||
            !NearlyEqual(live, slot.authoritative, 1.0e-4F)) {
            slot.history.Reset();
            slot.hasAuthoritative = false;
            ++result.invalid;
            continue;
        }
        bool validSubMeshes = true;
        for (std::size_t index = 0; index < slot.subMeshCount; ++index) {
            if (!ValidateSubMesh(slot, slot.subMeshes[index])) {
                validSubMeshes = false;
                break;
            }
        }
        if (!validSubMeshes) {
            ++result.invalid;
            continue;
        }

        const std::size_t bytes =
            vector.count * sizeof(transform::Matrix);
        std::memcpy(slot.restore.data(), live.bones.data(), bytes);
        if (!memory::TryCopy(vector.begin, sampled.pose.bones.data(),
                             bytes)) {
            (void)memory::TryCopy(vector.begin, slot.restore.data(), bytes);
            ++result.invalid;
            continue;
        }
        slot.overridden = true;
        if (!Regenerate(slot, result.regenerated, result.sleepingReskins)) {
            slot.overridden = false;
            if (!memory::TryCopy(vector.begin, slot.restore.data(), bytes)) {
                ++result.restoreFailed;
            }
            ++result.invalid;
            continue;
        }
        ++result.applied;
        if (slot.kind == Kind::ragdoll) {
            ++result.ragdollApplied;
        }
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
        VectorView vector{};
        const std::size_t bytes =
            slot.identityBoneCount * sizeof(transform::Matrix);
        if (!ValidateMesh(slot.mesh) || !ReadBoneVector(slot.mesh, vector) ||
            vector.begin != slot.identityBuffer ||
            vector.count != slot.identityBoneCount ||
            !memory::TryCopy(vector.begin, slot.restore.data(), bytes)) {
            ++result.restoreFailed;
            slot.overridden = false;
            continue;
        }
        if (!Regenerate(slot, result.regenerated, result.sleepingReskins)) {
            ++result.restoreFailed;
        }
        slot.overridden = false;
    }
    renderActive_ = false;
    return result;
}

} // namespace pbpfps::pose
