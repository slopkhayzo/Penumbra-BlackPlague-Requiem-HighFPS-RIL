#pragma once

#include "rigid_transform.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pbpfps::entity_root {

enum class NotifyResult : std::uint32_t {
    staged,
    ignored,
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
    static constexpr std::size_t kMaximumRoots = 64;

    void Configure(void* setMatrix,
                   void* meshGetModelMatrixTarget) noexcept;
    void Reset() noexcept;
    [[nodiscard]] NotifyResult NotifyMoved(
        void* entity, const transform::Matrix& matrix,
        std::uint64_t pendingSerial) noexcept;
    [[nodiscard]] BatchResult CommitCompletedTick(
        std::uint64_t tickSerial, std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] BatchResult BeginRender(
        const clock::Sample& clockSample) noexcept;
    [[nodiscard]] BatchResult EndRender() noexcept;

  private:
    struct Slot {
        void* entity = nullptr;
        transform::History history{};
        transform::Matrix staged{};
        transform::Matrix authoritative{};
        transform::Matrix restore{};
        std::uint64_t stagedSerial = 0;
        bool occupied = false;
        bool hasAuthoritative = false;
        bool overridden = false;
    };

    [[nodiscard]] Slot* FindOrCreate(void* entity) noexcept;
    [[nodiscard]] bool IsExpectedMeshEntity(void* entity) const noexcept;
    [[nodiscard]] bool ReadLocalMatrix(
        void* entity, transform::Matrix& matrix) const noexcept;
    [[nodiscard]] bool SetLocalMatrix(
        void* entity, const transform::Matrix& matrix) const noexcept;

    void* setMatrix_ = nullptr;
    void* meshGetModelMatrixTarget_ = nullptr;
    std::array<Slot, kMaximumRoots> slots_{};
    bool renderActive_ = false;
};

} // namespace pbpfps::entity_root
