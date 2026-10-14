#pragma once

#include "mouse_preview_math.h"

#include <cstddef>
#include <cstdint>

namespace pbpfps::gui_cursor {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

enum class OwnerKind : std::uint32_t {
    mainMenu,
    deathMenu,
    numericalPanel,
    notebook,
    inventory,
};

enum class FrameResult : std::uint32_t {
    applied,
    restored,
    stationary,
    noOwner,
    invalidInput,
    applyFailed,
    restoreFailed,
};

class Runtime {
  public:
    void Reset() noexcept;
    [[nodiscard]] FrameResult Apply(void* owner, OwnerKind kind, void* player,
                                    mouse::RawCounts pendingCounts,
                                    float sensitivity, float screenWidth,
                                    float screenHeight) noexcept;
    [[nodiscard]] FrameResult Restore() noexcept;

  private:
    void* overriddenOwner_ = nullptr;
    void* overriddenPlayer_ = nullptr;
    std::size_t ownerPositionOffset_ = 0;
    Position authoritativeOwnerPosition_{};
    Position authoritativePlayerPosition_{};
    bool mirrorsPlayer_ = false;
    bool overrideActive_ = false;
};

[[nodiscard]] const wchar_t* FrameResultName(FrameResult result) noexcept;

} // namespace pbpfps::gui_cursor
