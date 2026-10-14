#include "post_effect_timing.h"

#include <cmath>

namespace pbpfps::post_effect {

bool CorrectImageTrailAmount(const float stockAmount,
                             const float frameTimeSeconds,
                             const float referenceRateHz,
                             float& correctedAmount) noexcept {
    if (!std::isfinite(stockAmount) || stockAmount < 0.0F || stockAmount > 1.0F ||
        !std::isfinite(frameTimeSeconds) || frameTimeSeconds <= 0.0F ||
        !std::isfinite(referenceRateHz) || referenceRateHz <= 0.0F ||
        referenceRateHz > 1000.0F) {
        return false;
    }

    if (stockAmount == 0.0F || stockAmount == 1.0F) {
        correctedAmount = stockAmount;
        return true;
    }

    const double elapsedReferenceFrames =
        static_cast<double>(frameTimeSeconds) * static_cast<double>(referenceRateHz);
    const double corrected =
        std::exp(std::log(static_cast<double>(stockAmount)) * elapsedReferenceFrames);
    if (!std::isfinite(corrected) || corrected < 0.0 || corrected > 1.0) {
        return false;
    }
    correctedAmount = static_cast<float>(corrected);
    return std::isfinite(correctedAmount) && correctedAmount >= 0.0F &&
           correctedAmount <= 1.0F;
}

bool CorrectMotionBlurScale(const float stockScale,
                            const float frameTimeSeconds,
                            const float referenceRateHz,
                            float& correctedScale) noexcept {
    constexpr float kMaximumStockScale = 8.0F;
    constexpr float kMaximumCorrectedScale = 64.0F;
    if (!std::isfinite(stockScale) || stockScale < 0.0F ||
        stockScale > kMaximumStockScale ||
        !std::isfinite(frameTimeSeconds) || frameTimeSeconds <= 0.0F ||
        frameTimeSeconds > 1.0F || !std::isfinite(referenceRateHz) ||
        referenceRateHz <= 0.0F || referenceRateHz > 1000.0F) {
        return false;
    }
    const double corrected = static_cast<double>(stockScale) /
        (static_cast<double>(frameTimeSeconds) *
         static_cast<double>(referenceRateHz));
    if (!std::isfinite(corrected) || corrected < 0.0 ||
        corrected > kMaximumCorrectedScale) {
        return false;
    }
    correctedScale = static_cast<float>(corrected);
    return std::isfinite(correctedScale) && correctedScale >= 0.0F &&
           correctedScale <= kMaximumCorrectedScale;
}

} // namespace pbpfps::post_effect
