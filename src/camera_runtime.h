#pragma once

#include "camera_interpolation.h"

#include <cstdint>

namespace pbpfps::camera {

struct Functions {
    void* setPosition = nullptr;
    void* setPitch = nullptr;
    void* setYaw = nullptr;
    void* setRoll = nullptr;
};

enum class FrameResult : std::uint32_t {
    applied,
    restored,
    captured,
    noScene,
    noCamera,
    notCamera3D,
    identityChanged,
    invalidCamera,
    historyReset,
    clockUnavailable,
    snapshotsMissing,
    applyFailed,
    restoreFailed,
};

class Runtime {
  public:
    void Configure(const Functions& functions) noexcept;
    void Reset() noexcept;
    [[nodiscard]] FrameResult CaptureCompletedTick(
        std::uint64_t tickSerial, std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] FrameResult Apply(void* scene, std::uint64_t tickSerial,
                                    const clock::Sample& clockSample) noexcept;
    [[nodiscard]] bool ApplyAngularPreview(float pitchDelta,
                                           float yawDelta,
                                           Pose* before = nullptr,
                                           Pose* after = nullptr) noexcept;
    [[nodiscard]] FrameResult Restore() noexcept;

  private:
    struct AngularLimits {
        float pitchHigh = 0.0F;
        float pitchLow = 0.0F;
        float yawHigh = 0.0F;
        float yawLow = 0.0F;
    };

    [[nodiscard]] bool SelectCamera(void* scene, void*& camera,
                                    bool& is3D) const noexcept;
    [[nodiscard]] bool ReadPose(void* camera, Pose& pose,
                                AngularLimits* limits = nullptr) const noexcept;
    [[nodiscard]] bool SetPose(void* camera, const Pose& pose,
                               const AngularLimits& limits) const noexcept;

    Functions functions_{};
    History history_{};
    void* scene_ = nullptr;
    void* camera_ = nullptr;
    void* overriddenCamera_ = nullptr;
    Pose authoritativePose_{};
    AngularLimits authoritativeLimits_{};
    bool overrideActive_ = false;
};

[[nodiscard]] const wchar_t* FrameResultName(FrameResult result) noexcept;

} // namespace pbpfps::camera
