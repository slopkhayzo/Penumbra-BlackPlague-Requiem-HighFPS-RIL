#pragma once

#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pbpfps::interaction_cursor {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

enum class FrameResult : std::uint32_t {
    captured,
    applied,
    restored,
    ignoredState,
    noPlayer,
    inactivePlayer,
    invalidPlayer,
    identityChanged,
    historyReset,
    clockUnavailable,
    snapshotsMissing,
    currentMismatch,
    stationary,
    applyFailed,
    restoreFailed,
};

class Runtime {
  public:
    void Reset() noexcept;
    [[nodiscard]] FrameResult CaptureCompletedTick(
        void* player, std::uint64_t tickSerial,
        std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] FrameResult Apply(
        void* player, const clock::Sample& clockSample) noexcept;
    [[nodiscard]] FrameResult Restore() noexcept;

    [[nodiscard]] static bool IsEligiblePlayerState(int state) noexcept;

  private:
    struct Snapshot {
        std::uint64_t serial = 0;
        Position position{};
    };

    [[nodiscard]] bool ReadPlayer(void* player, bool& active, int& state,
                                  int& crossHairState,
                                  Position& position) const noexcept;
    [[nodiscard]] bool WritePosition(void* player,
                                     const Position& position) const noexcept;
    void Rebase(void* player, int state, int crossHairState,
                std::uint64_t serial,
                std::uint64_t generation, const Position& position) noexcept;
    void Append(std::uint64_t serial, const Position& position) noexcept;
    [[nodiscard]] const Snapshot& At(std::size_t index) const noexcept;

    std::array<Snapshot, clock::PresentationClock::kHistoryCapacity>
        snapshots_{};
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    void* player_ = nullptr;
    int playerState_ = 0;
    int crossHairState_ = 0;
    std::uint64_t generation_ = 0;
    bool hasIdentity_ = false;
    void* overriddenPlayer_ = nullptr;
    Position authoritativePosition_{};
    bool overrideActive_ = false;
};

[[nodiscard]] const wchar_t* FrameResultName(FrameResult result) noexcept;

} // namespace pbpfps::interaction_cursor
