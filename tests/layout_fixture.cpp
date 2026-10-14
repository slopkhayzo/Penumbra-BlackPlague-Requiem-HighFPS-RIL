#include "target_engine_layout.h"

#include <cstdlib>

// Layout fixture for the runtime unit tests. The fake objects in those tests
// were written against the validated Overture build, so the fixture keeps
// Overture's game-layer values. It is test-only: no shipped adapter uses it.
namespace {

constexpr pbpfps::target::Layout BuildOvertureFixture() noexcept {
    pbpfps::target::Layout layout = pbpfps::target::EngineLayout();
    layout.handsCurrentModels = 0x6C;
    layout.handsCurrentModelCount = 0x74;
    layout.handsExpectedModelCount = 2;
    layout.hudModelEntity = 0x118;

    layout.handlerSensitivity = 0x20;
    layout.handlerInvertY = 0x24;
    layout.handlerInit = 0x2C;
    layout.handlerPlayer = 0x3C;
    layout.handlerState = 0x40;
    layout.handlerStateGame = 1;
    layout.handlerStateMainMenu = 2;

    layout.initHasHaptics = 0x84;
    layout.initPlayer = 0x18C;
    layout.initInventory = 0x194;
    layout.initNotebook = 0x1A8;
    layout.initNumericalPanel = 0x1AC;
    layout.initDeathMenu = 0x1B0;
    layout.initMainMenu = 0x1D8;
    layout.initGameGui = pbpfps::target::kAbsent;
    layout.gameGuiActiveSet = 0;

    layout.inventoryActive = 0x5C;
    layout.inventoryMessageActive = 0xB4;
    layout.notebookActive = 0x44;
    layout.numericalPanelActive = 0x31;
    layout.deathMenuActive = 0x2D;
    layout.mainMenuActionText = 0x60;
    layout.mainMenuCursor = 0xD4;
    layout.deathMenuCursor = 0x38;
    layout.numericalPanelCursor = 0x38;
    layout.notebookCursor = 0x84;
    layout.inventoryCursor = 0xAC;

    layout.playerActive = 0x1E4;
    layout.playerCamera = 0x1E8;
    layout.playerLookSpeed = 0x1EC;
    layout.playerDeath = 0x298;
    layout.playerDeathActive = 0x00;
    layout.playerState = 0x2C8;
    layout.playerCrossHairState = 0x2F0;
    layout.playerCrossHairPosition = 0x2F4;
    layout.playerStateNormal = 0;
    layout.playerStateCount = 10;
    layout.crossHairStateCount = 12;
    layout.interactionCursorStates =
        (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4) | (1U << 6);
    return layout;
}

constexpr pbpfps::target::Layout kOvertureFixture = BuildOvertureFixture();

// Selected before main() so each test binary runs against one fixed layout.
const bool g_selected = [] {
    if (!pbpfps::target::SelectLayout(kOvertureFixture)) {
        std::abort();
    }
    return true;
}();

} // namespace
