#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pbpfps::mesh {

enum class ObserveResult : std::uint32_t {
    added,
    duplicate,
    inactive,
    capacityExhausted,
};

struct RefreshResult {
    std::uint32_t refreshed = 0;
    std::uint32_t invalid = 0;
};

class Cache {
  public:
    static constexpr std::size_t kMaximumEntities = 1024;

    void Configure(void* getModelMatrixTrampoline,
                   void* getModelMatrixTarget,
                   void* getInvModelMatrix) noexcept;
    void Reset() noexcept;
    void BeginCollection() noexcept;
    void EndCollection() noexcept;
    [[nodiscard]] ObserveResult Observe(void* entity) noexcept;
    [[nodiscard]] RefreshResult Refresh() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept { return count_; }

  private:
    [[nodiscard]] bool IsExpectedMeshEntity(void* entity) const noexcept;

    void* getModelMatrixTrampoline_ = nullptr;
    void* getModelMatrixTarget_ = nullptr;
    void* getInvModelMatrix_ = nullptr;
    // Open-addressed membership set so each GetModelMatrix observation is
    // O(1); the renderer queries every visible entity many times per list.
    static constexpr std::size_t kBucketCount = kMaximumEntities * 4;

    [[nodiscard]] static std::size_t Bucket(const void* entity) noexcept;

    std::array<void*, kMaximumEntities> entities_{};
    std::array<void*, kBucketCount> buckets_{};
    std::size_t count_ = 0;
    bool collecting_ = false;
};

} // namespace pbpfps::mesh
