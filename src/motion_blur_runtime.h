#pragma once

namespace pbpfps::motion_blur {

enum class BeginKind {
    inactive,
    unavailable,
    firstPass,
    applied,
    invalid,
    reentrant,
};

struct BeginResult {
    BeginKind kind = BeginKind::invalid;
    float stockScale = 0.0F;
    float correctedScale = 0.0F;
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
    float stockScale_ = 0.0F;
    bool overrideActive_ = false;
};

} // namespace pbpfps::motion_blur
