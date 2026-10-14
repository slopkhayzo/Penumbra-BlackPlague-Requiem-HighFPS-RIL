#include "motion_blur_runtime.h"

#include "post_effect_timing.h"
#include "target_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::motion_blur {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }

constexpr float kReferenceRateHz = 60.0F;

template <typename T>
T ReadAt(const void* const object, const std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value,
                static_cast<const std::uint8_t*>(object) + offset,
                sizeof(value));
    return value;
}

template <typename T>
void WriteAt(void* const object, const std::size_t offset,
             const T& value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(object) + offset,
                &value, sizeof(value));
}

} // namespace

BeginResult Runtime::Begin(void* const renderer,
                           const float frameTimeSeconds) noexcept {
    if (overrideActive_) {
        return {BeginKind::reentrant, stockScale_, stockScale_};
    }
    if (renderer == nullptr) {
        return {};
    }
    const std::uint8_t active =
        ReadAt<std::uint8_t>(renderer, Fields().motionBlurActive);
    const std::uint8_t firstPass =
        ReadAt<std::uint8_t>(renderer, Fields().motionBlurFirstPass);
    const float stockScale = ReadAt<float>(renderer, Fields().motionBlurScale);
    if (active > 1 || firstPass > 1) {
        return {BeginKind::invalid, stockScale, stockScale};
    }
    if (active == 0) {
        return {BeginKind::inactive, stockScale, stockScale};
    }
    void* const vertexProgram =
        ReadAt<void*>(renderer, Fields().motionBlurVertexProgram);
    void* const fragmentProgram =
        ReadAt<void*>(renderer, Fields().motionBlurFragmentProgram);
    if (vertexProgram == nullptr || fragmentProgram == nullptr) {
        return {BeginKind::unavailable, stockScale, stockScale};
    }
    if (firstPass != 0) {
        return {BeginKind::firstPass, stockScale, stockScale};
    }

    float correctedScale = 0.0F;
    if (!post_effect::CorrectMotionBlurScale(
            stockScale, frameTimeSeconds, kReferenceRateHz,
            correctedScale)) {
        return {BeginKind::invalid, stockScale, stockScale};
    }
    renderer_ = renderer;
    stockScale_ = stockScale;
    overrideActive_ = true;
    WriteAt(renderer_, Fields().motionBlurScale, correctedScale);
    return {BeginKind::applied, stockScale, correctedScale};
}

EndKind Runtime::End() noexcept {
    if (!overrideActive_ || renderer_ == nullptr) {
        return EndKind::none;
    }
    WriteAt(renderer_, Fields().motionBlurScale, stockScale_);
    const float restored = ReadAt<float>(renderer_, Fields().motionBlurScale);
    const bool exact =
        std::memcmp(&restored, &stockScale_, sizeof(restored)) == 0;
    renderer_ = nullptr;
    stockScale_ = 0.0F;
    overrideActive_ = false;
    return exact ? EndKind::restored : EndKind::restoreFailed;
}

void Runtime::Reset() noexcept {
    if (overrideActive_ && renderer_ != nullptr) {
        WriteAt(renderer_, Fields().motionBlurScale, stockScale_);
    }
    renderer_ = nullptr;
    stockScale_ = 0.0F;
    overrideActive_ = false;
}

} // namespace pbpfps::motion_blur
