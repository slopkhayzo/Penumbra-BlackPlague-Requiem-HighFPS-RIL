#include "interaction_cursor_runtime.h"

#include "guarded_memory.h"
#include "target_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::interaction_cursor {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }


bool IsValidPosition(const Position& position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) &&
           position.x >= 0.0F && position.x <= 800.0F &&
           position.y >= 0.0F && position.y <= 600.0F;
}

bool NearlyEqual(const Position& left, const Position& right,
                 const float epsilon = 1.0e-4F) noexcept {
    return std::abs(left.x - right.x) <= epsilon &&
           std::abs(left.y - right.y) <= epsilon;
}

Position Interpolate(const Position& older, const Position& newer,
                     const double alpha) noexcept {
    const float amount = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    return {older.x + (newer.x - older.x) * amount,
            older.y + (newer.y - older.y) * amount};
}

} // namespace

void Runtime::Reset() noexcept {
    first_ = 0;
    count_ = 0;
    player_ = nullptr;
    playerState_ = 0;
    crossHairState_ = 0;
    generation_ = 0;
    hasIdentity_ = false;
    overriddenPlayer_ = nullptr;
    authoritativePosition_ = {};
    overrideActive_ = false;
}

bool Runtime::IsEligiblePlayerState(const int state) noexcept {
    // The states whose OnAddYaw/OnAddPitch route the mouse to the
    // interaction cursor. Normal look and the message/weapon/throw/climb
    // modes keep their own presentation paths.
    return state >= 0 && state < 32 &&
           ((Fields().interactionCursorStates >> state) & 1U) != 0;
}

bool Runtime::ReadPlayer(void* const player, bool& active, int& state,
                         int& crossHairState,
                         Position& position) const noexcept {
    std::uint8_t activeByte = 0;
    if (!memory::TryReadAt(player, Fields().playerActive, activeByte) ||
        !memory::TryReadAt(player, Fields().playerState, state) ||
        !memory::TryReadAt(player, Fields().playerCrossHairState, crossHairState) ||
        !memory::TryReadAt(player, Fields().playerCrossHairPosition, position)) {
        return false;
    }
    active = activeByte != 0;
    return state >= 0 && state < Fields().playerStateCount &&
           crossHairState >= 0 &&
           crossHairState < Fields().crossHairStateCount &&
           IsValidPosition(position);
}

bool Runtime::WritePosition(void* const player,
                            const Position& position) const noexcept {
    return IsValidPosition(position) &&
           memory::TryWriteAt(player, Fields().playerCrossHairPosition, position);
}

const Runtime::Snapshot& Runtime::At(const std::size_t index) const noexcept {
    return snapshots_[(first_ + index) % snapshots_.size()];
}

void Runtime::Append(const std::uint64_t serial,
                     const Position& position) noexcept {
    if (count_ < snapshots_.size()) {
        snapshots_[(first_ + count_) % snapshots_.size()] = {serial, position};
        ++count_;
        return;
    }
    snapshots_[first_] = {serial, position};
    first_ = (first_ + 1U) % snapshots_.size();
}

void Runtime::Rebase(void* const player, const int state,
                     const int crossHairState,
                     const std::uint64_t serial,
                     const std::uint64_t generation,
                     const Position& position) noexcept {
    first_ = 0;
    count_ = 0;
    player_ = player;
    playerState_ = state;
    crossHairState_ = crossHairState;
    generation_ = generation;
    hasIdentity_ = true;
    Append(serial, position);
}

FrameResult Runtime::CaptureCompletedTick(
    void* const player, const std::uint64_t tickSerial,
    const std::uint64_t clockGeneration) noexcept {
    if (overrideActive_) {
        return FrameResult::applyFailed;
    }
    if (player == nullptr) {
        Reset();
        return FrameResult::noPlayer;
    }
    bool active = false;
    int state = 0;
    int crossHairState = 0;
    Position position{};
    if (!ReadPlayer(player, active, state, crossHairState, position)) {
        Reset();
        return FrameResult::invalidPlayer;
    }
    if (!active) {
        Reset();
        return FrameResult::inactivePlayer;
    }
    if (!IsEligiblePlayerState(state)) {
        Reset();
        return FrameResult::ignoredState;
    }
    if (!hasIdentity_ || player_ != player || playerState_ != state ||
        crossHairState_ != crossHairState ||
        generation_ != clockGeneration) {
        Rebase(player, state, crossHairState, tickSerial, clockGeneration,
               position);
        return FrameResult::identityChanged;
    }

    const Snapshot& latest = At(count_ - 1U);
    if (tickSerial <= latest.serial || tickSerial != latest.serial + 1U) {
        Rebase(player, state, crossHairState, tickSerial, clockGeneration,
               position);
        return FrameResult::historyReset;
    }
    Append(tickSerial, position);
    return FrameResult::captured;
}

FrameResult Runtime::Apply(void* const player,
                           const clock::Sample& sample) noexcept {
    if (overrideActive_) {
        return FrameResult::applyFailed;
    }
    if (player == nullptr) {
        return FrameResult::noPlayer;
    }
    bool active = false;
    int state = 0;
    int crossHairState = 0;
    Position current{};
    if (!ReadPlayer(player, active, state, crossHairState, current)) {
        return FrameResult::invalidPlayer;
    }
    if (!active) {
        return FrameResult::inactivePlayer;
    }
    if (!IsEligiblePlayerState(state)) {
        return FrameResult::ignoredState;
    }
    if (!hasIdentity_ || player_ != player || playerState_ != state ||
        crossHairState_ != crossHairState) {
        return FrameResult::identityChanged;
    }
    if (sample.kind != clock::SampleKind::interpolated ||
        sample.generation != generation_) {
        return FrameResult::clockUnavailable;
    }

    const Snapshot* older = nullptr;
    const Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < count_; ++index) {
        const Snapshot& candidate = At(index);
        if (candidate.serial == sample.olderSerial) {
            older = &candidate;
        }
        if (candidate.serial == sample.newerSerial) {
            newer = &candidate;
        }
    }
    if (older == nullptr || newer == nullptr) {
        return FrameResult::snapshotsMissing;
    }
    if (!NearlyEqual(current, newer->position)) {
        return FrameResult::currentMismatch;
    }
    if (NearlyEqual(older->position, newer->position)) {
        return FrameResult::stationary;
    }

    const Position interpolated =
        Interpolate(older->position, newer->position, sample.alpha);
    authoritativePosition_ = current;
    overriddenPlayer_ = player;
    if (!WritePosition(overriddenPlayer_, interpolated)) {
        overriddenPlayer_ = nullptr;
        return FrameResult::applyFailed;
    }
    overrideActive_ = true;
    return FrameResult::applied;
}

FrameResult Runtime::Restore() noexcept {
    if (!overrideActive_) {
        return FrameResult::restored;
    }
    const bool restored =
        WritePosition(overriddenPlayer_, authoritativePosition_);
    overrideActive_ = false;
    overriddenPlayer_ = nullptr;
    return restored ? FrameResult::restored : FrameResult::restoreFailed;
}

const wchar_t* FrameResultName(const FrameResult result) noexcept {
    switch (result) {
    case FrameResult::captured: return L"captured";
    case FrameResult::applied: return L"applied";
    case FrameResult::restored: return L"restored";
    case FrameResult::ignoredState: return L"ignored-state";
    case FrameResult::noPlayer: return L"no-player";
    case FrameResult::inactivePlayer: return L"inactive-player";
    case FrameResult::invalidPlayer: return L"invalid-player";
    case FrameResult::identityChanged: return L"identity-changed";
    case FrameResult::historyReset: return L"history-reset";
    case FrameResult::clockUnavailable: return L"clock-unavailable";
    case FrameResult::snapshotsMissing: return L"snapshots-missing";
    case FrameResult::currentMismatch: return L"current-mismatch";
    case FrameResult::stationary: return L"stationary";
    case FrameResult::applyFailed: return L"apply-failed";
    case FrameResult::restoreFailed: return L"restore-failed";
    }
    return L"unknown";
}

} // namespace pbpfps::interaction_cursor
