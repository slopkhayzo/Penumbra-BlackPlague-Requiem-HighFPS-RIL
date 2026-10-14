#pragma once

#include "rigid_transform.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pbpfps::world {

enum class NotifyResult : std::uint32_t {
    staged,
    invalid,
    capacityExhausted,
};

struct BatchResult {
    std::uint32_t captured = 0;
    std::uint32_t reset = 0;
    std::uint32_t invalid = 0;
    std::uint32_t ready = 0;
    std::uint32_t pairMiss = 0;
    std::uint32_t applyFailed = 0;
    std::uint32_t restoreFailed = 0;
};

class Runtime {
  public:
    static constexpr std::size_t kMaximumNodes = 256;
    static constexpr std::uint64_t kStaleTicks = 600;

    void Configure(void* setMatrix) noexcept;
    void Reset() noexcept;
    [[nodiscard]] NotifyResult NotifyNodeMoved(
        void* node, const transform::Matrix& matrix,
        std::uint64_t pendingSerial) noexcept;
    [[nodiscard]] BatchResult CommitCompletedTick(
        std::uint64_t tickSerial, std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] BatchResult BeginRender(
        const clock::Sample& clockSample) noexcept;
    [[nodiscard]] BatchResult EndRender() noexcept;

  private:
    struct Slot {
        void* node = nullptr;
        // First word of the node when it was last seen moving.
        const void* vtable = nullptr;
        transform::History history{};
        transform::Matrix staged{};
        transform::Matrix authoritative{};
        transform::Matrix restore{};
        std::uint64_t stagedSerial = 0;
        std::uint64_t lastSeenSerial = 0;
        bool occupied = false;
        bool hasAuthoritative = false;
        bool overridden = false;
    };

    [[nodiscard]] Slot* FindOrCreate(void* node,
                                     std::uint64_t pendingSerial) noexcept;
    [[nodiscard]] bool IsSameObject(const Slot& slot) const noexcept;
    [[nodiscard]] bool ReadLocalMatrix(void* node,
                                       transform::Matrix& matrix) const noexcept;
    [[nodiscard]] bool SetLocalMatrix(
        void* node, const transform::Matrix& matrix) const noexcept;

    void* setMatrix_ = nullptr;
    std::array<Slot, kMaximumNodes> slots_{};
    bool renderActive_ = false;
};

} // namespace pbpfps::world
