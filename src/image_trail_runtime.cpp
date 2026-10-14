#include "image_trail_runtime.h"

#include "post_effect_timing.h"
#include "target_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::image_trail {
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
        return {BeginKind::reentrant, stockAmount_, stockAmount_};
    }
    if (renderer == nullptr) {
        return {};
    }

    const std::uint8_t active = ReadAt<std::uint8_t>(renderer, Fields().imageTrailActive);
    const std::uint8_t firstPass =
        ReadAt<std::uint8_t>(renderer, Fields().imageTrailFirstPass);
    const float stockAmount = ReadAt<float>(renderer, Fields().imageTrailAmount);
    if (active > 1 || firstPass > 1) {
        return {BeginKind::invalid, stockAmount, stockAmount};
    }
    if (active == 0) {
        return {BeginKind::inactive, stockAmount, stockAmount};
    }
    if (firstPass != 0) {
        return {BeginKind::firstPass, stockAmount, stockAmount};
    }

    float correctedAmount = 0.0F;
    if (!post_effect::CorrectImageTrailAmount(
            stockAmount, frameTimeSeconds, kReferenceRateHz,
            correctedAmount)) {
        return {BeginKind::invalid, stockAmount, stockAmount};
    }

    renderer_ = renderer;
    stockAmount_ = stockAmount;
    overrideActive_ = true;
    WriteAt(renderer_, Fields().imageTrailAmount, correctedAmount);
    return {BeginKind::applied, stockAmount, correctedAmount};
}

EndKind Runtime::End() noexcept {
    if (!overrideActive_ || renderer_ == nullptr) {
        return EndKind::none;
    }
    WriteAt(renderer_, Fields().imageTrailAmount, stockAmount_);
    const float restored = ReadAt<float>(renderer_, Fields().imageTrailAmount);
    const bool exact = std::memcmp(&restored, &stockAmount_,
                                   sizeof(restored)) == 0;
    renderer_ = nullptr;
    stockAmount_ = 0.0F;
    overrideActive_ = false;
    return exact ? EndKind::restored : EndKind::restoreFailed;
}

void Runtime::Reset() noexcept {
    if (overrideActive_ && renderer_ != nullptr) {
        WriteAt(renderer_, Fields().imageTrailAmount, stockAmount_);
    }
    renderer_ = nullptr;
    stockAmount_ = 0.0F;
    overrideActive_ = false;
}

} // namespace pbpfps::image_trail
