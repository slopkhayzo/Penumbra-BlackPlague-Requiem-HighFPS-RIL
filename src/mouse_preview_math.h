#pragma once

#include "presentation_clock.h"

#include <cstdint>

namespace pbpfps::mouse {

struct RawCounts {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

struct AngularDelta {
    float pitch = 0.0F;
    float yaw = 0.0F;
};

struct CursorDelta {
    float x = 0.0F;
    float y = 0.0F;
};

// SDL relative counts are drained at presentation rate and returned to the
// unchanged fixed-tick mouse update exactly once. These helpers keep that
// conservation arithmetic bounded and explicit.
[[nodiscard]] bool Accumulate(RawCounts& pending, std::int32_t x,
                              std::int32_t y) noexcept;
[[nodiscard]] bool CombineForDelivery(RawCounts pending, std::int32_t freshX,
                                      std::int32_t freshY,
                                      std::int32_t& deliveredX,
                                      std::int32_t& deliveredY) noexcept;

// The game converts raw SDL counts to virtual coordinates and immediately
// divides by the same virtual size in cButtonHandler::Update. The net normal
// look scale is therefore raw / physical-screen-size * sensitivity *
// mfLookSpeed, with cPlayer applying the signs below.
[[nodiscard]] bool NormalLookAngles(RawCounts counts, float sensitivity,
                                    float lookSpeed, float screenWidth,
                                    float screenHeight, bool invertY,
                                    bool equalizeAxes,
                                    AngularDelta& result) noexcept;

// Retail divides horizontal mouse counts by physical width and vertical
// counts by physical height. Scaling yaw by width / height makes both axes use
// physical height while preserving the stock vertical sensitivity.
[[nodiscard]] bool AxisEqualizationScale(float screenWidth,
                                         float screenHeight,
                                         float& result) noexcept;

// UI AddMousePos receives the SDL count converted into HPL's 800x600 virtual
// coordinates, then multiplied by the configured mouse sensitivity.
[[nodiscard]] bool GuiCursorDelta(RawCounts counts, float sensitivity,
                                  float screenWidth, float screenHeight,
                                  CursorDelta& result) noexcept;

// Once a fixed tick has incorporated a previewed delta, the interpolated base
// camera contains alpha of it. Retain only (1-alpha) as a render-only bridge
// so the view does not snap backward at the delivery boundary.
[[nodiscard]] bool ResidualForSample(
    AngularDelta delivered, std::uint64_t deliveredSerial,
    std::uint64_t deliveredGeneration, const clock::Sample& sample,
    AngularDelta& result) noexcept;

} // namespace pbpfps::mouse
