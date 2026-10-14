#pragma once

#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pbpfps::particle {

struct RuntimeFunctions {
    void* updateGraphics = nullptr;
    void* updateGraphicsTarget = nullptr;
};

enum class ObserveResult : std::uint32_t {
    added,
    addedLine,
    duplicate,
    ignoredBeam,
    invalid,
    capacityExhausted,
};

struct BatchResult {
    std::uint32_t targets = 0;
    std::uint32_t particles = 0;
    std::uint32_t applied = 0;
    std::uint32_t regenerated = 0;
    std::uint32_t clockWait = 0;
    std::uint32_t stationarySkipped = 0;
    std::uint32_t invalid = 0;
    std::uint32_t restoreFailed = 0;
};

class Runtime {
  public:
    static constexpr std::size_t kMaximumEmitters = 128;
    static constexpr std::size_t kMaximumParticlesPerEmitter = 1024;

    void Configure(const RuntimeFunctions& functions) noexcept;
    void Reset() noexcept;
    void BeginCollection() noexcept;
    [[nodiscard]] ObserveResult Observe(void* emitter, void* camera,
                                        float frameTime,
                                        void* renderList) noexcept;
    void EndCollection() noexcept;
    [[nodiscard]] BatchResult Present(
        const clock::Sample& clockSample) noexcept;

  private:
    struct Call {
        void* emitter = nullptr;
        void* camera = nullptr;
        void* renderList = nullptr;
        float frameTime = 0.0F;
        // Line emitters draw a streak from the previous to the current
        // position, so both endpoints move together.
        bool line = false;
    };

    struct Vector3 {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
    };

    struct RestoreEntry {
        void* particle = nullptr;
        Vector3 position{};
        Vector3 interpolated{};
        Vector3 lastPosition{};
        Vector3 interpolatedLast{};
    };

    [[nodiscard]] bool ValidateEmitter(void* emitter, bool line,
                                       std::uint32_t& activeCount,
                                       void**& particles,
                                       std::uint8_t*& dirty) const noexcept;
    [[nodiscard]] bool CaptureParticles(void** particles, std::uint32_t count,
                                        float alpha) noexcept;
    [[nodiscard]] bool WriteParticles(std::uint32_t count, bool interpolated,
                                      bool line) noexcept;
    [[nodiscard]] bool PresentOne(const Call& call, double alpha,
                                  BatchResult& result) noexcept;

    RuntimeFunctions functions_{};
    std::array<Call, kMaximumEmitters> calls_{};
    std::array<RestoreEntry, kMaximumParticlesPerEmitter> restore_{};
    std::size_t callCount_ = 0;
    bool collecting_ = false;
    bool presenting_ = false;
};

} // namespace pbpfps::particle
