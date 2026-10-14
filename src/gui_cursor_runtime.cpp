#include "gui_cursor_runtime.h"

#include "guarded_memory.h"
#include "target_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::gui_cursor {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }

struct OwnerFields {
    std::size_t positionOffset = 0;
    bool mirrorsPlayer = false;
};

OwnerFields OwnerLayout(const OwnerKind kind) noexcept {
    const target::Layout& fields = Fields();
    switch (kind) {
    case OwnerKind::mainMenu: return {fields.mainMenuCursor, false};
    case OwnerKind::deathMenu: return {fields.deathMenuCursor, true};
    case OwnerKind::numericalPanel:
        return {fields.numericalPanelCursor, true};
    case OwnerKind::notebook: return {fields.notebookCursor, true};
    case OwnerKind::inventory: return {fields.inventoryCursor, true};
    }
    return {};
}

bool ValidPosition(const Position& position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) &&
           position.x >= 0.0F && position.x <= 800.0F &&
           position.y >= 0.0F && position.y <= 600.0F;
}

bool ReadPosition(const void* object, const std::size_t offset,
                  Position& position) noexcept {
    return memory::TryReadAt(object, static_cast<std::ptrdiff_t>(offset),
                             position) &&
           ValidPosition(position);
}

bool WritePosition(void* object, const std::size_t offset,
                   const Position& position) noexcept {
    return ValidPosition(position) &&
           memory::TryWriteAt(object, static_cast<std::ptrdiff_t>(offset),
                              position);
}

} // namespace

void Runtime::Reset() noexcept {
    overriddenOwner_ = nullptr;
    overriddenPlayer_ = nullptr;
    ownerPositionOffset_ = 0;
    authoritativeOwnerPosition_ = {};
    authoritativePlayerPosition_ = {};
    mirrorsPlayer_ = false;
    overrideActive_ = false;
}

FrameResult Runtime::Apply(void* const owner, const OwnerKind kind,
                           void* const player,
                           const mouse::RawCounts pendingCounts,
                           const float sensitivity, const float screenWidth,
                           const float screenHeight) noexcept {
    if (overrideActive_) {
        return FrameResult::applyFailed;
    }
    if (pendingCounts.x == 0 && pendingCounts.y == 0) {
        return FrameResult::stationary;
    }
    const OwnerFields layout = OwnerLayout(kind);
    if (owner == nullptr || layout.positionOffset == 0 ||
        (layout.mirrorsPlayer && player == nullptr)) {
        return FrameResult::invalidInput;
    }

    Position ownerPosition{};
    Position playerPosition{};
    mouse::CursorDelta delta{};
    if (!ReadPosition(owner, layout.positionOffset, ownerPosition) ||
        (layout.mirrorsPlayer &&
         !ReadPosition(player, Fields().playerCrossHairPosition, playerPosition)) ||
        !mouse::GuiCursorDelta(pendingCounts, sensitivity, screenWidth,
                               screenHeight, delta)) {
        return FrameResult::invalidInput;
    }
    const Position presented{
        std::clamp(ownerPosition.x + delta.x, 0.0F, 800.0F),
        std::clamp(ownerPosition.y + delta.y, 0.0F, 600.0F),
    };
    if (presented.x == ownerPosition.x && presented.y == ownerPosition.y) {
        return FrameResult::stationary;
    }

    overriddenOwner_ = owner;
    overriddenPlayer_ = player;
    ownerPositionOffset_ = layout.positionOffset;
    authoritativeOwnerPosition_ = ownerPosition;
    authoritativePlayerPosition_ = playerPosition;
    mirrorsPlayer_ = layout.mirrorsPlayer;
    if (!WritePosition(overriddenOwner_, ownerPositionOffset_, presented)) {
        Reset();
        return FrameResult::applyFailed;
    }
    if (mirrorsPlayer_ &&
        !WritePosition(overriddenPlayer_, Fields().playerCrossHairPosition, presented)) {
        (void)WritePosition(overriddenOwner_, ownerPositionOffset_,
                            authoritativeOwnerPosition_);
        Reset();
        return FrameResult::applyFailed;
    }
    overrideActive_ = true;
    return FrameResult::applied;
}

FrameResult Runtime::Restore() noexcept {
    if (!overrideActive_) {
        return FrameResult::restored;
    }
    const bool ownerRestored =
        WritePosition(overriddenOwner_, ownerPositionOffset_,
                      authoritativeOwnerPosition_);
    const bool playerRestored =
        !mirrorsPlayer_ ||
        WritePosition(overriddenPlayer_, Fields().playerCrossHairPosition,
                      authoritativePlayerPosition_);
    Reset();
    return ownerRestored && playerRestored ? FrameResult::restored
                                           : FrameResult::restoreFailed;
}

const wchar_t* FrameResultName(const FrameResult result) noexcept {
    switch (result) {
    case FrameResult::applied: return L"applied";
    case FrameResult::restored: return L"restored";
    case FrameResult::stationary: return L"stationary";
    case FrameResult::noOwner: return L"no-owner";
    case FrameResult::invalidInput: return L"invalid-input";
    case FrameResult::applyFailed: return L"apply-failed";
    case FrameResult::restoreFailed: return L"restore-failed";
    }
    return L"unknown";
}

} // namespace pbpfps::gui_cursor
