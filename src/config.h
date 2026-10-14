#pragma once

#include <string>

namespace pbpfps {

struct FeatureSettings {
    bool renderUnlock = false;
    bool presentationLimiter = false;
    bool cameraInterpolation = false;
    bool handsRootInterpolation = false;
    bool worldRootInterpolation = false;
    bool handsPoseInterpolation = false;
    bool worldPoseInterpolation = false;
    bool ragdollPoseInterpolation = false;
    bool particleInterpolation = false;
    bool imageTrailTiming = false;
    bool motionBlurTiming = false;
    bool mouseOverlay = false;
    bool mouseAxisCorrection = false;
    bool interactionCursorInterpolation = false;
    bool guiMouseOverlay = false;
};

struct Settings {
    bool enabled = true;
    bool diagnostics = true;
    bool telemetry = true;
    bool motionTrace = false;
    int reportIntervalSeconds = 5;
    int targetFps = 0;
    FeatureSettings features{};
};

// overrideSection, when given, names a per-executable section ([BlackPlague]
// or [Requiem]) whose feature keys take precedence over [Features].
[[nodiscard]] Settings LoadSettings(
    const std::wstring& path, const wchar_t* overrideSection = nullptr) noexcept;
[[nodiscard]] bool AnyBehaviorFeatureEnabled(const FeatureSettings& features) noexcept;

} // namespace pbpfps
