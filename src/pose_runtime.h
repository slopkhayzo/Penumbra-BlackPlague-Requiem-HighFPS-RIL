#pragma once

#include "pose_interpolation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pbpfps::pose {

struct RuntimeFeatures {
    bool hands = false;
    bool world = false;
    bool ragdoll = false;
};

struct RuntimeFunctions {
    void* subMeshUpdateGraphics = nullptr;
    void* meshUpdateGraphicsTarget = nullptr;
    void* subMeshUpdateGraphicsTarget = nullptr;
};

enum class ObserveMeshResult : std::uint32_t {
    captured,
    unchanged,
    ignored,
    historyReset,
    invalid,
    capacityExhausted,
};

enum class ObserveSubMeshResult : std::uint32_t {
    added,
    duplicate,
    ignored,
    invalid,
    capacityExhausted,
};

struct BatchResult {
    std::uint32_t targets = 0;
    std::uint32_t applied = 0;
    std::uint32_t regenerated = 0;
    std::uint32_t clockWait = 0;
    std::uint32_t pairMissing = 0;
    std::uint32_t stationarySkipped = 0;
    std::uint32_t invalid = 0;
    std::uint32_t restoreFailed = 0;
    // Diagnostics for skeleton-physics meshes: how many presentations
    // overrode a ragdoll, and how many submesh re-skins were forced while the
    // engine itself considered the ragdoll asleep (stock never re-skins then).
    std::uint32_t ragdollApplied = 0;
    std::uint32_t sleepingReskins = 0;
};

class Runtime {
  public:
    static constexpr std::size_t kMaximumMeshes = 16;
    static constexpr std::size_t kMaximumSubMeshes = 16;
    static constexpr std::size_t kMaximumHandsMeshes = 4;

    void Configure(const RuntimeFunctions& functions,
                   const RuntimeFeatures& features) noexcept;
    void Reset() noexcept;

    void MarkHandsMesh(void* mesh, std::uint64_t pendingSerial) noexcept;
    void BeginCollection(std::uint64_t tickSerial,
                         std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] ObserveMeshResult ObserveMesh(void* mesh) noexcept;
    [[nodiscard]] ObserveSubMeshResult ObserveSubMesh(
        void* subMesh, void* camera, float frameTime,
        void* renderList) noexcept;
    void EndCollection() noexcept;

    [[nodiscard]] BatchResult BeginRender(
        const clock::Sample& clockSample) noexcept;
    [[nodiscard]] BatchResult EndRender() noexcept;

  private:
    enum class Kind : std::uint8_t {
        hands,
        world,
        ragdoll,
    };

    struct SubMeshCall {
        void* subMesh = nullptr;
        void* camera = nullptr;
        void* renderList = nullptr;
        float frameTime = 0.0F;
    };

    struct Slot {
        void* mesh = nullptr;
        transform::Matrix* identityBuffer = nullptr;
        std::size_t identityBoneCount = 0;
        History history{};
        Pose authoritative{};
        std::array<transform::Matrix, kMaximumBones> restore{};
        std::array<SubMeshCall, kMaximumSubMeshes> subMeshes{};
        std::size_t subMeshCount = 0;
        std::uint64_t lastCapturedSerial = 0;
        std::uint64_t observedEpoch = 0;
        Kind kind = Kind::world;
        bool occupied = false;
        bool hasAuthoritative = false;
        bool changedOnLastCapture = false;
        bool overridden = false;
    };

    struct HandsMarker {
        void* mesh = nullptr;
        std::uint64_t serial = 0;
    };

    [[nodiscard]] Slot* Find(void* mesh) noexcept;
    [[nodiscard]] Slot* FindOrRecycle(void* mesh) noexcept;
    [[nodiscard]] bool IsHandsMesh(void* mesh) const noexcept;
    [[nodiscard]] bool IsKindEnabled(Kind kind) const noexcept;
    [[nodiscard]] bool ValidateMesh(void* mesh) const noexcept;
    [[nodiscard]] bool ValidateSubMesh(const Slot& slot,
                                       const SubMeshCall& call) const noexcept;
    [[nodiscard]] bool Regenerate(Slot& slot, std::uint32_t& calls,
                                  std::uint32_t& sleepingCalls) const noexcept;
    void Clear(Slot& slot) noexcept;

    RuntimeFunctions functions_{};
    RuntimeFeatures features_{};
    std::array<Slot, kMaximumMeshes> slots_{};
    std::array<HandsMarker, kMaximumHandsMeshes> handsMarkers_{};
    std::uint64_t tickSerial_ = 0;
    std::uint64_t clockGeneration_ = 0;
    std::uint64_t epoch_ = 0;
    bool collecting_ = false;
    bool renderActive_ = false;
};

} // namespace pbpfps::pose
