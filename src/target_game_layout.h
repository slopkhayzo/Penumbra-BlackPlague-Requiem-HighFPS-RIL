#pragma once

#include "target_engine_layout.h"

namespace pbpfps::target {

// The four cPlayer members that sit at different offsets in the two
// executables; everything else in the game layer is common to both.
struct PlayerTail {
    std::size_t death;
    std::size_t state;
    std::size_t crossHairState;
    std::size_t crossHairPosition;
};

// Game-layer fields common to Black Plague and Requiem. Both were built from
// the same episode-2 game code: cButtonHandler::Update differs between them
// only in the cPlayer state offset, and the cInit members it reaches are at
// the same offsets in both.
[[nodiscard]] constexpr Layout Episode2Layout(const PlayerTail& player) noexcept {
    Layout layout = EngineLayout();

    // cPlayerHands::Update differs from Overture only in cInit and cPlayer
    // offsets, so the HUD-model fields it reads are unchanged.
    layout.handsCurrentModels = 0x6C;
    layout.handsCurrentModelCount = 0x74;
    layout.handsExpectedModelCount = 2;
    layout.hudModelEntity = 0x118;

    // cButtonHandler lost its haptic pointer, so the two trailing members
    // moved up by four bytes relative to Overture.
    layout.handlerSensitivity = 0x20;
    layout.handlerInvertY = 0x24;
    layout.handlerInit = 0x2C;
    layout.handlerPlayer = 0x38;
    layout.handlerState = 0x3C;
    layout.handlerStateGame = 1;
    layout.handlerStateMainMenu = 2;

    // cInit. There is no haptic flag: cButtonHandler::Update never tests one.
    layout.initHasHaptics = kAbsent;
    layout.initPlayer = 0x15C;
    layout.initInventory = 0x164;
    layout.initNotebook = 0x178;
    layout.initNumericalPanel = 0x17C;
    layout.initDeathMenu = 0x180;
    layout.initMainMenu = 0x1B8;
    // "GameGuiHandler": the in-game engine-GUI owner. It takes the mouse
    // while its active-set pointer is non-null.
    layout.initGameGui = 0x1AC;
    layout.gameGuiActiveSet = 0x30;

    layout.inventoryActive = 0x5C;
    layout.inventoryMessageActive = 0xB4;
    layout.notebookActive = 0x44;
    layout.numericalPanelActive = 0x31;
    layout.deathMenuActive = 0x2D;
    layout.mainMenuActionText = 0x60;
    layout.mainMenuCursor = 0xA0;
    layout.deathMenuCursor = 0x38;
    layout.numericalPanelCursor = 0x38;
    layout.notebookCursor = 0x84;
    layout.inventoryCursor = 0xAC;

    layout.playerActive = 0x1DC;
    layout.playerCamera = 0x1E0;
    layout.playerLookSpeed = 0x1E4;
    layout.playerDeath = player.death;
    layout.playerDeathActive = 0x00;
    layout.playerState = player.state;
    layout.playerCrossHairState = player.crossHairState;
    layout.playerCrossHairPosition = player.crossHairPosition;

    // The ten-entry state table keeps Overture's numbering: Normal, Push,
    // Move, InteractMode, UseItem, Message, Grab, WeaponMelee, Throw, Climb.
    layout.playerStateNormal = 0;
    layout.playerStateCount = 10;
    layout.crossHairStateCount = 12;
    // Push, Move, InteractMode, UseItem, Grab.
    layout.interactionCursorStates =
        (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4) | (1U << 6);
    return layout;
}

} // namespace pbpfps::target
