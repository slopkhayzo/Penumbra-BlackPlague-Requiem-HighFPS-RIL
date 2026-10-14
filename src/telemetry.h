#pragma once

#include "target_mapping.h"

#include <string>

namespace pbpfps::telemetry {

[[nodiscard]] bool Install(const target::Mapping& mapping,
                           int reportIntervalSeconds,
                           bool telemetryEnabled,
                           bool renderUnlockEnabled,
                           bool cameraInterpolationEnabled,
                           bool handsRootInterpolationEnabled,
                           bool worldRootInterpolationEnabled,
                           bool handsPoseInterpolationEnabled,
                           bool worldPoseInterpolationEnabled,
                           bool ragdollPoseInterpolationEnabled,
                           bool particleInterpolationEnabled,
                           bool imageTrailTimingEnabled,
                           bool motionBlurTimingEnabled,
                           bool mouseOverlayEnabled,
                           bool mouseAxisCorrectionEnabled,
                           bool interactionCursorInterpolationEnabled,
                           bool guiMouseOverlayEnabled,
                           std::wstring& error) noexcept;
void Shutdown() noexcept;

} // namespace pbpfps::telemetry
