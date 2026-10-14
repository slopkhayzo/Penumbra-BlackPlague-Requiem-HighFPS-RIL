#include "camera_interpolation.h"

#include <algorithm>
#include <cmath>

namespace pbpfps::camera {
namespace {

constexpr float kTwoPi = 6.28318530717958647692F;

float ShortestAngleDelta(const float from, const float to) noexcept {
    return std::remainder(to - from, kTwoPi);
}

bool IsDiscontinuous(const Pose& older, const Pose& newer) noexcept {
    const float dx = newer.position.x - older.position.x;
    const float dy = newer.position.y - older.position.y;
    const float dz = newer.position.z - older.position.z;
    const float maximumPosition = History::kMaximumPositionDeltaPerTick;
    if (dx * dx + dy * dy + dz * dz > maximumPosition * maximumPosition) {
        return true;
    }
    return OrientationDistance(older, newer) >
           History::kMaximumOrientationDeltaPerTick;
}

} // namespace

bool IsFinite(const Pose& pose) noexcept {
    return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
           std::isfinite(pose.position.z) && std::isfinite(pose.pitch) &&
           std::isfinite(pose.yaw) && std::isfinite(pose.roll);
}

bool NearlyEqual(const Pose& left, const Pose& right,
                 const float epsilon) noexcept {
    return std::abs(left.position.x - right.position.x) <= epsilon &&
           std::abs(left.position.y - right.position.y) <= epsilon &&
           std::abs(left.position.z - right.position.z) <= epsilon &&
           std::abs(ShortestAngleDelta(left.pitch, right.pitch)) <= epsilon &&
           std::abs(ShortestAngleDelta(left.yaw, right.yaw)) <= epsilon &&
           std::abs(ShortestAngleDelta(left.roll, right.roll)) <= epsilon;
}

float OrientationDistance(const Pose& left, const Pose& right) noexcept {
    const float pitch = ShortestAngleDelta(left.pitch, right.pitch);
    const float yaw = ShortestAngleDelta(left.yaw, right.yaw);
    const float roll = ShortestAngleDelta(left.roll, right.roll);
    return std::sqrt(pitch * pitch + yaw * yaw + roll * roll);
}

float RebaseAngleNear(const float value, const float reference) noexcept {
    if (!std::isfinite(value) || !std::isfinite(reference)) {
        return value;
    }
    return value + std::round((reference - value) / kTwoPi) * kTwoPi;
}

bool Interpolate(const Pose& older, const Pose& newer, const double alpha,
                 Pose& result) noexcept {
    if (!IsFinite(older) || !IsFinite(newer) || !std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        return false;
    }
    const float t = static_cast<float>(alpha);
    result.position = {
        older.position.x + (newer.position.x - older.position.x) * t,
        older.position.y + (newer.position.y - older.position.y) * t,
        older.position.z + (newer.position.z - older.position.z) * t,
    };
    result.pitch = older.pitch + ShortestAngleDelta(older.pitch, newer.pitch) * t;
    result.yaw = older.yaw + ShortestAngleDelta(older.yaw, newer.yaw) * t;
    result.roll = older.roll + ShortestAngleDelta(older.roll, newer.roll) * t;
    return IsFinite(result);
}

void History::Reset() noexcept {
    first_ = 0;
    count_ = 0;
    generation_ = 0;
    hasGeneration_ = false;
}

const History::Snapshot& History::At(const std::size_t logicalIndex) const noexcept {
    return snapshots_[(first_ + logicalIndex) % kCapacity];
}

void History::Append(const std::uint64_t serial, const Pose& pose) noexcept {
    if (count_ < kCapacity) {
        snapshots_[(first_ + count_) % kCapacity] = {serial, pose};
        ++count_;
        return;
    }
    snapshots_[first_] = {serial, pose};
    first_ = (first_ + 1) % kCapacity;
}

PublishResult History::Publish(const std::uint64_t serial,
                               const std::uint64_t clockGeneration,
                               const Pose& pose) noexcept {
    if (!IsFinite(pose)) {
        Reset();
        return {PublishKind::invalid};
    }
    if (!hasGeneration_ || generation_ != clockGeneration) {
        Reset();
        generation_ = clockGeneration;
        hasGeneration_ = true;
        Append(serial, pose);
        return {PublishKind::resetGeneration};
    }
    if (count_ != 0) {
        const Snapshot& latest = At(count_ - 1);
        if (serial < latest.serial) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetNonMonotonic};
        }
        if (serial == latest.serial) {
            if (NearlyEqual(latest.pose, pose)) {
                return {PublishKind::unchanged};
            }
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetSameTickMutation};
        }
        if (serial != latest.serial + 1U) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetMissingTick};
        }
        if (IsDiscontinuous(latest.pose, pose)) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetDiscontinuity};
        }
    }
    Append(serial, pose);
    return {PublishKind::appended};
}

SampleResult History::Evaluate(const clock::Sample& clockSample) const noexcept {
    if (clockSample.kind != clock::SampleKind::interpolated ||
        clockSample.olderSerial == clockSample.newerSerial) {
        return {SampleKind::clockUnavailable, {}};
    }
    if (!hasGeneration_ || generation_ != clockSample.generation) {
        return {SampleKind::generationMismatch, {}};
    }
    const Snapshot* older = nullptr;
    const Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < count_; ++index) {
        const Snapshot& candidate = At(index);
        if (candidate.serial == clockSample.olderSerial) {
            older = &candidate;
        }
        if (candidate.serial == clockSample.newerSerial) {
            newer = &candidate;
        }
    }
    if (older == nullptr || newer == nullptr) {
        return {SampleKind::snapshotsMissing, {}};
    }
    Pose interpolated{};
    if (!Interpolate(older->pose, newer->pose, clockSample.alpha,
                     interpolated)) {
        return {SampleKind::invalid, {}};
    }
    return {SampleKind::applied, interpolated};
}

const wchar_t* PublishKindName(const PublishKind kind) noexcept {
    switch (kind) {
    case PublishKind::appended: return L"appended";
    case PublishKind::unchanged: return L"unchanged";
    case PublishKind::invalid: return L"invalid";
    case PublishKind::resetGeneration: return L"generation";
    case PublishKind::resetNonMonotonic: return L"non-monotonic";
    case PublishKind::resetSameTickMutation: return L"same-tick-mutation";
    case PublishKind::resetMissingTick: return L"missing-tick";
    case PublishKind::resetDiscontinuity: return L"discontinuity";
    }
    return L"unknown";
}

const wchar_t* SampleKindName(const SampleKind kind) noexcept {
    switch (kind) {
    case SampleKind::applied: return L"applied";
    case SampleKind::clockUnavailable: return L"clock-unavailable";
    case SampleKind::generationMismatch: return L"generation-mismatch";
    case SampleKind::snapshotsMissing: return L"snapshots-missing";
    case SampleKind::invalid: return L"invalid";
    }
    return L"unknown";
}

} // namespace pbpfps::camera
