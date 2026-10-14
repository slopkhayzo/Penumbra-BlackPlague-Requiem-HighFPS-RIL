#pragma once

namespace pbpfps::image_trail {

enum class BeginKind {
    inactive,
    firstPass,
    applied,
    invalid,
    reentrant,
};

struct BeginResult {
    BeginKind kind = BeginKind::invalid;
    float stockAmount = 0.0F;
    float correctedAmount = 0.0F;
};

enum class EndKind {
    none,
    restored,
    restoreFailed,
};

class Runtime {
  public:
    [[nodiscard]] BeginResult Begin(void* renderer,
                                    float frameTimeSeconds) noexcept;
    [[nodiscard]] EndKind End() noexcept;
    void Reset() noexcept;

  private:
    void* renderer_ = nullptr;
    float stockAmount_ = 0.0F;
    bool overrideActive_ = false;
};

} // namespace pbpfps::image_trail
