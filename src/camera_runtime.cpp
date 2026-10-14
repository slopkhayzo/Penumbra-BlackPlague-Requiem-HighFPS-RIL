#include "camera_runtime.h"

#include "guarded_memory.h"
#include "image_range.h"
#include "target_layout.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace pbpfps::camera {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }


// A camera's GetType target must be code inside the host executable. The
// range comes from the mapped PE headers once; no page query is issued.
bool IsExecutable(const void* address) noexcept {
    return memory::IsMainImageCode(address);
}

template <typename T>
bool ReadAt(const void* base, const std::size_t offset, T& value) noexcept {
    return memory::TryReadAt(base, static_cast<std::ptrdiff_t>(offset),
                             value);
}

bool LimitsAreFinite(const float pitchHigh, const float pitchLow,
                     const float yawHigh, const float yawLow) noexcept {
    constexpr float kMaximumSaneAngle = 1000.0F;
    return std::isfinite(pitchHigh) && std::isfinite(pitchLow) &&
           std::isfinite(yawHigh) && std::isfinite(yawLow) &&
           std::abs(pitchHigh) <= kMaximumSaneAngle &&
           std::abs(pitchLow) <= kMaximumSaneAngle &&
           std::abs(yawHigh) <= kMaximumSaneAngle &&
           std::abs(yawLow) <= kMaximumSaneAngle;
}

} // namespace

void Runtime::Configure(const Functions& functions) noexcept {
    functions_ = functions;
    Reset();
}

void Runtime::Reset() noexcept {
    history_.Reset();
    scene_ = nullptr;
    camera_ = nullptr;
    overriddenCamera_ = nullptr;
    authoritativePose_ = {};
    authoritativeLimits_ = {};
    overrideActive_ = false;
}

FrameResult Runtime::CaptureCompletedTick(
    const std::uint64_t tickSerial,
    const std::uint64_t clockGeneration) noexcept {
    if (scene_ == nullptr) {
        return FrameResult::noScene;
    }
    void* selected = nullptr;
    bool is3D = false;
    if (!SelectCamera(scene_, selected, is3D)) {
        history_.Reset();
        camera_ = nullptr;
        return FrameResult::noCamera;
    }
    if (!is3D) {
        history_.Reset();
        camera_ = nullptr;
        return FrameResult::notCamera3D;
    }
    Pose current{};
    if (!ReadPose(selected, current)) {
        history_.Reset();
        camera_ = nullptr;
        return FrameResult::invalidCamera;
    }
    if (camera_ != selected) {
        history_.Reset();
        camera_ = selected;
        (void)history_.Publish(tickSerial, clockGeneration, current);
        return FrameResult::identityChanged;
    }
    const PublishResult published =
        history_.Publish(tickSerial, clockGeneration, current);
    if (published.kind == PublishKind::appended ||
        published.kind == PublishKind::unchanged) {
        return FrameResult::captured;
    }
    return FrameResult::historyReset;
}

bool Runtime::SelectCamera(void* scene, void*& camera, bool& is3D) const noexcept {
    camera = nullptr;
    is3D = false;
    if (!ReadAt(scene, Fields().sceneActiveCamera, camera) || camera == nullptr) {
        return false;
    }
    void** vtable = nullptr;
    void* getTypeAddress = nullptr;
    if (!ReadAt(camera, 0, vtable) || vtable == nullptr ||
        !ReadAt(vtable, Fields().cameraGetTypeVtableIndex * sizeof(void*), getTypeAddress) ||
        !IsExecutable(getTypeAddress)) {
        return false;
    }
    using GetTypeFn = int(__thiscall*)(void*);
    const auto getType = reinterpret_cast<GetTypeFn>(getTypeAddress);
    is3D = getType(camera) == Fields().cameraType3D;
    return true;
}

bool Runtime::ReadPose(void* camera, Pose& pose,
                       AngularLimits* const limits) const noexcept {
    std::uint32_t rotateMode = 0;
    AngularLimits readLimits{};
    if (!ReadAt(camera, Fields().cameraPosition, pose.position) ||
        !ReadAt(camera, Fields().cameraPitch, pose.pitch) ||
        !ReadAt(camera, Fields().cameraYaw, pose.yaw) ||
        !ReadAt(camera, Fields().cameraRoll, pose.roll) ||
        !ReadAt(camera, Fields().cameraPitchHigh, readLimits.pitchHigh) ||
        !ReadAt(camera, Fields().cameraPitchLow, readLimits.pitchLow) ||
        !ReadAt(camera, Fields().cameraYawHigh, readLimits.yawHigh) ||
        !ReadAt(camera, Fields().cameraYawLow, readLimits.yawLow) ||
        !ReadAt(camera, Fields().cameraRotateMode, rotateMode) ||
        rotateMode != Fields().cameraEulerAnglesMode || !IsFinite(pose) ||
        !LimitsAreFinite(readLimits.pitchHigh, readLimits.pitchLow,
                        readLimits.yawHigh, readLimits.yawLow)) {
        return false;
    }
    constexpr float kMaximumSanePosition = 10000000.0F;
    if (std::abs(pose.position.x) > kMaximumSanePosition ||
        std::abs(pose.position.y) > kMaximumSanePosition ||
        std::abs(pose.position.z) > kMaximumSanePosition) {
        return false;
    }
    if (limits != nullptr) {
        *limits = readLimits;
    }
    return true;
}

bool Runtime::SetPose(void* camera, const Pose& pose,
                      const AngularLimits& limits) const noexcept {
    if (camera == nullptr || !IsFinite(pose) ||
        functions_.setPosition == nullptr || functions_.setPitch == nullptr ||
        functions_.setYaw == nullptr || functions_.setRoll == nullptr) {
        return false;
    }
    using SetPositionFn = void(__thiscall*)(void*, const Vec3&);
    using SetAngleFn = void(__thiscall*)(void*, float);
    const auto setPosition =
        reinterpret_cast<SetPositionFn>(functions_.setPosition);
    const auto setPitch = reinterpret_cast<SetAngleFn>(functions_.setPitch);
    const auto setYaw = reinterpret_cast<SetAngleFn>(functions_.setYaw);
    const auto setRoll = reinterpret_cast<SetAngleFn>(functions_.setRoll);

    Pose target = pose;
    if (limits.yawHigh != 0.0F || limits.yawLow != 0.0F) {
        target.yaw = RebaseAngleNear(target.yaw, authoritativePose_.yaw);
    }
    setPosition(camera, target.position);
    setPitch(camera, target.pitch);
    setYaw(camera, target.yaw);
    setRoll(camera, target.roll);

    Pose applied{};
    return ReadPose(camera, applied) && NearlyEqual(applied, target, 1.0e-4F);
}

FrameResult Runtime::Apply(void* scene, const std::uint64_t tickSerial,
                           const clock::Sample& clockSample) noexcept {
    if (overrideActive_) {
        return FrameResult::applyFailed;
    }
    scene_ = scene;
    void* selected = nullptr;
    bool is3D = false;
    if (!SelectCamera(scene, selected, is3D)) {
        history_.Reset();
        camera_ = nullptr;
        return FrameResult::noCamera;
    }
    if (!is3D) {
        history_.Reset();
        camera_ = nullptr;
        return FrameResult::notCamera3D;
    }

    Pose current{};
    AngularLimits limits{};
    if (!ReadPose(selected, current, &limits)) {
        history_.Reset();
        camera_ = nullptr;
        return FrameResult::invalidCamera;
    }
    if (camera_ != selected) {
        history_.Reset();
        camera_ = selected;
        (void)history_.Publish(tickSerial, clockSample.generation, current);
        return FrameResult::identityChanged;
    }

    const SampleResult sampled = history_.Evaluate(clockSample);
    if (sampled.kind == SampleKind::clockUnavailable ||
        sampled.kind == SampleKind::generationMismatch) {
        return FrameResult::clockUnavailable;
    }
    if (sampled.kind == SampleKind::snapshotsMissing) {
        return FrameResult::snapshotsMissing;
    }
    if (sampled.kind != SampleKind::applied) {
        return FrameResult::invalidCamera;
    }

    authoritativePose_ = current;
    authoritativeLimits_ = limits;
    overriddenCamera_ = selected;
    if (!SetPose(overriddenCamera_, sampled.pose, authoritativeLimits_)) {
        (void)SetPose(overriddenCamera_, authoritativePose_,
                      authoritativeLimits_);
        overriddenCamera_ = nullptr;
        return FrameResult::applyFailed;
    }
    overrideActive_ = true;
    return FrameResult::applied;
}

bool Runtime::ApplyAngularPreview(const float pitchDelta,
                                  const float yawDelta, Pose* const before,
                                  Pose* const after) noexcept {
    if (!overrideActive_ || overriddenCamera_ == nullptr ||
        !std::isfinite(pitchDelta) || !std::isfinite(yawDelta)) {
        return false;
    }
    Pose pose{};
    if (!ReadPose(overriddenCamera_, pose)) {
        return false;
    }
    const Pose original = pose;
    pose.pitch += pitchDelta;
    pose.yaw += yawDelta;
    if (!IsFinite(pose) ||
        !SetPose(overriddenCamera_, pose, authoritativeLimits_)) {
        return false;
    }
    Pose applied{};
    if (!ReadPose(overriddenCamera_, applied)) {
        return false;
    }
    if (before != nullptr) {
        *before = original;
    }
    if (after != nullptr) {
        *after = applied;
    }
    return true;
}

FrameResult Runtime::Restore() noexcept {
    if (!overrideActive_) {
        return FrameResult::restored;
    }
    const bool restored = SetPose(overriddenCamera_, authoritativePose_,
                                  authoritativeLimits_);
    overrideActive_ = false;
    overriddenCamera_ = nullptr;
    return restored ? FrameResult::restored : FrameResult::restoreFailed;
}

const wchar_t* FrameResultName(const FrameResult result) noexcept {
    switch (result) {
    case FrameResult::applied: return L"applied";
    case FrameResult::restored: return L"restored";
    case FrameResult::captured: return L"captured";
    case FrameResult::noScene: return L"no-scene";
    case FrameResult::noCamera: return L"no-camera";
    case FrameResult::notCamera3D: return L"not-camera-3d";
    case FrameResult::identityChanged: return L"identity-changed";
    case FrameResult::invalidCamera: return L"invalid-camera";
    case FrameResult::historyReset: return L"history-reset";
    case FrameResult::clockUnavailable: return L"clock-unavailable";
    case FrameResult::snapshotsMissing: return L"snapshots-missing";
    case FrameResult::applyFailed: return L"apply-failed";
    case FrameResult::restoreFailed: return L"restore-failed";
    }
    return L"unknown";
}

} // namespace pbpfps::camera
