#include "mouse_preview_math.h"

#include <cmath>
#include <limits>

namespace pbpfps::mouse {
namespace {

constexpr std::int64_t kMaximumPendingCounts = 1LL << 30;

bool ValidComponent(const std::int64_t value) noexcept {
    return value >= -kMaximumPendingCounts && value <= kMaximumPendingCounts;
}

} // namespace

bool Accumulate(RawCounts& pending, const std::int32_t x,
                const std::int32_t y) noexcept {
    const std::int64_t nextX = pending.x + static_cast<std::int64_t>(x);
    const std::int64_t nextY = pending.y + static_cast<std::int64_t>(y);
    if (!ValidComponent(nextX) || !ValidComponent(nextY)) {
        return false;
    }
    pending = {nextX, nextY};
    return true;
}

bool CombineForDelivery(const RawCounts pending, const std::int32_t freshX,
                        const std::int32_t freshY,
                        std::int32_t& deliveredX,
                        std::int32_t& deliveredY) noexcept {
    const std::int64_t x = pending.x + static_cast<std::int64_t>(freshX);
    const std::int64_t y = pending.y + static_cast<std::int64_t>(freshY);
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() ||
        y > std::numeric_limits<std::int32_t>::max()) {
        deliveredX = freshX;
        deliveredY = freshY;
        return false;
    }
    deliveredX = static_cast<std::int32_t>(x);
    deliveredY = static_cast<std::int32_t>(y);
    return true;
}

bool NormalLookAngles(const RawCounts counts, const float sensitivity,
                      const float lookSpeed, const float screenWidth,
                      const float screenHeight, const bool invertY,
                      const bool equalizeAxes,
                      AngularDelta& result) noexcept {
    result = {};
    if (!std::isfinite(sensitivity) || !std::isfinite(lookSpeed) ||
        !std::isfinite(screenWidth) || !std::isfinite(screenHeight) ||
        sensitivity < 0.0F || lookSpeed < 0.0F || screenWidth <= 0.0F ||
        screenHeight <= 0.0F) {
        return false;
    }
    const double common = static_cast<double>(sensitivity) *
                          static_cast<double>(lookSpeed);
    const double yawDenominator = static_cast<double>(
        equalizeAxes ? screenHeight : screenWidth);
    const double yaw = -(static_cast<double>(counts.x) / yawDenominator) *
                       common;
    double pitch = -(static_cast<double>(counts.y) /
                     static_cast<double>(screenHeight)) *
                   common;
    if (invertY) {
        pitch = -pitch;
    }
    if (!std::isfinite(yaw) || !std::isfinite(pitch) ||
        yaw < -std::numeric_limits<float>::max() ||
        yaw > std::numeric_limits<float>::max() ||
        pitch < -std::numeric_limits<float>::max() ||
        pitch > std::numeric_limits<float>::max()) {
        return false;
    }
    result = {static_cast<float>(pitch), static_cast<float>(yaw)};
    return true;
}

bool AxisEqualizationScale(const float screenWidth, const float screenHeight,
                           float& result) noexcept {
    result = 1.0F;
    if (!std::isfinite(screenWidth) || !std::isfinite(screenHeight) ||
        screenWidth <= 0.0F || screenHeight <= 0.0F) {
        return false;
    }
    const double scale = static_cast<double>(screenWidth) /
                         static_cast<double>(screenHeight);
    if (!std::isfinite(scale) || scale <= 0.0 ||
        scale > std::numeric_limits<float>::max()) {
        return false;
    }
    result = static_cast<float>(scale);
    return true;
}

bool GuiCursorDelta(const RawCounts counts, const float sensitivity,
                    const float screenWidth, const float screenHeight,
                    CursorDelta& result) noexcept {
    result = {};
    if (!std::isfinite(sensitivity) || !std::isfinite(screenWidth) ||
        !std::isfinite(screenHeight) || sensitivity < 0.0F ||
        screenWidth <= 0.0F || screenHeight <= 0.0F) {
        return false;
    }
    const double x = static_cast<double>(counts.x) * 800.0 /
                     static_cast<double>(screenWidth) *
                     static_cast<double>(sensitivity);
    const double y = static_cast<double>(counts.y) * 600.0 /
                     static_cast<double>(screenHeight) *
                     static_cast<double>(sensitivity);
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < -std::numeric_limits<float>::max() ||
        x > std::numeric_limits<float>::max() ||
        y < -std::numeric_limits<float>::max() ||
        y > std::numeric_limits<float>::max()) {
        return false;
    }
    result = {static_cast<float>(x), static_cast<float>(y)};
    return true;
}

bool ResidualForSample(const AngularDelta delivered,
                       const std::uint64_t deliveredSerial,
                       const std::uint64_t deliveredGeneration,
                       const clock::Sample& sample,
                       AngularDelta& result) noexcept {
    result = {};
    if (!std::isfinite(delivered.pitch) || !std::isfinite(delivered.yaw) ||
        sample.kind != clock::SampleKind::interpolated ||
        sample.generation != deliveredGeneration ||
        sample.newerSerial != deliveredSerial ||
        !std::isfinite(sample.alpha) || sample.alpha < 0.0 ||
        sample.alpha > 1.0) {
        return false;
    }
    const float remaining = static_cast<float>(1.0 - sample.alpha);
    result = {delivered.pitch * remaining, delivered.yaw * remaining};
    return true;
}

} // namespace pbpfps::mouse
