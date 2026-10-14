#include "presentation_clock.h"

#include <algorithm>
#include <cmath>

namespace pbpfps::clock {
namespace {

constexpr double kTimeEpsilon = 1.0e-9;

} // namespace

PresentationClock::PresentationClock(const double nativeStepSeconds,
                                     const double longStallSeconds) noexcept {
    if (std::isfinite(nativeStepSeconds) && nativeStepSeconds > 0.0 &&
        nativeStepSeconds <= 1.0) {
        nativeStepSeconds_ = nativeStepSeconds;
    }
    if (std::isfinite(longStallSeconds) &&
        longStallSeconds > nativeStepSeconds_ && longStallSeconds <= 60.0) {
        longStallSeconds_ = longStallSeconds;
    }
}

void PresentationClock::Clear(const ResetReason reason) noexcept {
    first_ = 0;
    count_ = 0;
    hasLastSample_ = false;
    lastResetReason_ = reason;
    ++generation_;
}

void PresentationClock::Reset(const ResetReason reason) noexcept { Clear(reason); }

const PresentationClock::Snapshot&
PresentationClock::At(const std::size_t logicalIndex) const noexcept {
    return snapshots_[(first_ + logicalIndex) % kHistoryCapacity];
}

void PresentationClock::Append(const std::uint64_t serial,
                               const double wallTimeSeconds) noexcept {
    const Snapshot snapshot{serial, wallTimeSeconds};
    if (count_ < kHistoryCapacity) {
        snapshots_[(first_ + count_) % kHistoryCapacity] = snapshot;
        ++count_;
        return;
    }
    snapshots_[first_] = snapshot;
    first_ = (first_ + 1) % kHistoryCapacity;
}

PublishResult PresentationClock::Publish(const std::uint64_t serial,
                                         const double wallTimeSeconds,
                                         const bool discontinuity) noexcept {
    ResetReason reason = ResetReason::none;
    if (!std::isfinite(wallTimeSeconds)) {
        Clear(ResetReason::nonMonotonicWallTime);
        return {false, ResetReason::nonMonotonicWallTime};
    }

    if (discontinuity) {
        Clear(ResetReason::explicitDiscontinuity);
        reason = ResetReason::explicitDiscontinuity;
    } else if (count_ != 0) {
        const Snapshot& latest = At(count_ - 1);
        if (serial <= latest.serial) {
            Clear(ResetReason::nonMonotonicTick);
            reason = ResetReason::nonMonotonicTick;
        } else if (serial != latest.serial + 1U) {
            Clear(ResetReason::missingSnapshot);
            reason = ResetReason::missingSnapshot;
        } else if (wallTimeSeconds + kTimeEpsilon < latest.wallArrivalTimeSeconds) {
            Clear(ResetReason::nonMonotonicWallTime);
            reason = ResetReason::nonMonotonicWallTime;
        } else if (wallTimeSeconds - latest.wallArrivalTimeSeconds > longStallSeconds_) {
            Clear(ResetReason::longStall);
            reason = ResetReason::longStall;
        }
    }

    Append(serial, wallTimeSeconds);
    return {true, reason};
}

Sample PresentationClock::Evaluate(const double wallTimeSeconds,
                                   const double interpolationAlpha,
                                   const bool phaseValid) noexcept {
    Sample result{};
    result.generation = generation_;
    if (!std::isfinite(wallTimeSeconds)) {
        Clear(ResetReason::nonMonotonicWallTime);
        result.kind = SampleKind::reset;
        result.resetReason = ResetReason::nonMonotonicWallTime;
        result.generation = generation_;
        return result;
    }

    if (hasLastSample_ &&
        wallTimeSeconds + kTimeEpsilon < lastSampleWallTimeSeconds_) {
        Clear(ResetReason::nonMonotonicWallTime);
        result.kind = SampleKind::reset;
        result.resetReason = ResetReason::nonMonotonicWallTime;
        result.generation = generation_;
        return result;
    }
    if (hasLastSample_ &&
        wallTimeSeconds - lastSampleWallTimeSeconds_ > longStallSeconds_) {
        Clear(ResetReason::longStall);
        result.kind = SampleKind::reset;
        result.resetReason = ResetReason::longStall;
        result.generation = generation_;
        return result;
    }
    lastSampleWallTimeSeconds_ = wallTimeSeconds;
    hasLastSample_ = true;

    if (count_ == 0) {
        return result;
    }
    const Snapshot& latest = At(count_ - 1);
    if (wallTimeSeconds - latest.wallArrivalTimeSeconds > longStallSeconds_) {
        Clear(ResetReason::longStall);
        result.kind = SampleKind::reset;
        result.resetReason = ResetReason::longStall;
        result.generation = generation_;
        return result;
    }
    if (count_ != kHistoryCapacity || !phaseValid ||
        !std::isfinite(interpolationAlpha)) {
        return result;
    }

    const Snapshot& oldest = At(0);
    if (latest.serial != oldest.serial + 1U) {
        Clear(ResetReason::missingSnapshot);
        result.kind = SampleKind::reset;
        result.resetReason = ResetReason::missingSnapshot;
        result.generation = generation_;
        return result;
    }

    result.kind = SampleKind::interpolated;
    result.olderSerial = oldest.serial;
    result.newerSerial = latest.serial;
    result.alpha = std::clamp(interpolationAlpha, 0.0, 1.0);
    result.targetTimeSeconds =
        (static_cast<double>(oldest.serial) + result.alpha) * nativeStepSeconds_;
    return result;
}

bool LogicTimerAlpha(const double nextTickMilliseconds,
                     const double stepMilliseconds,
                     const double speedMultiplier,
                     const double nowMilliseconds,
                     const double presentationDelayTicks,
                     double& alpha,
                     double* const rawAlpha) noexcept {
    alpha = 0.0;
    if (rawAlpha != nullptr) {
        *rawAlpha = 0.0;
    }
    if (!std::isfinite(nextTickMilliseconds) ||
        !std::isfinite(stepMilliseconds) ||
        !std::isfinite(speedMultiplier) || !std::isfinite(nowMilliseconds) ||
        !std::isfinite(presentationDelayTicks) || stepMilliseconds < 1.0 ||
        stepMilliseconds > 1000.0 || speedMultiplier < 0.01 ||
        speedMultiplier > 100.0 || presentationDelayTicks < 0.0 ||
        presentationDelayTicks > 1.0) {
        return false;
    }

    const double interval = stepMilliseconds / speedMultiplier;
    const double raw =
        (nowMilliseconds - (nextTickMilliseconds - interval)) / interval;
    const double delayed = raw - presentationDelayTicks;
    if (rawAlpha != nullptr) {
        *rawAlpha = raw;
    }
    if (!std::isfinite(raw) || !std::isfinite(delayed) || raw < -0.25 ||
        raw > 2.25 || delayed < -0.25 || delayed > 1.25) {
        return false;
    }
    alpha = std::clamp(delayed, 0.0, 1.0);
    return true;
}

const wchar_t* ResetReasonName(const ResetReason reason) noexcept {
    switch (reason) {
    case ResetReason::none: return L"none";
    case ResetReason::explicitDiscontinuity: return L"explicit-discontinuity";
    case ResetReason::nonMonotonicWallTime: return L"non-monotonic-wall-time";
    case ResetReason::nonMonotonicTick: return L"non-monotonic-tick";
    case ResetReason::missingSnapshot: return L"missing-snapshot";
    case ResetReason::longStall: return L"long-stall";
    }
    return L"unknown";
}

const wchar_t* SampleKindName(const SampleKind kind) noexcept {
    switch (kind) {
    case SampleKind::unavailable: return L"unavailable";
    case SampleKind::interpolated: return L"interpolated";
    case SampleKind::reset: return L"reset";
    }
    return L"unknown";
}

} // namespace pbpfps::clock
