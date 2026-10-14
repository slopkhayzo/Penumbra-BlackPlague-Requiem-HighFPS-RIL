#pragma once

#include <cstddef>
#include <cstdint>

// Every object offset, vtable index, and enum value the runtimes read from
// game memory. One table is selected for the host executable at startup and
// is immutable afterwards; reusable math and runtimes hold no layout
// constants of their own.
namespace pbpfps::target {

// A field the target does not have. Readers must treat it as "feature absent"
// rather than as offset zero.
inline constexpr std::size_t kAbsent = static_cast<std::size_t>(-1);

struct Layout {
    // cGame
    std::size_t gameLimitFps;

    // cLogicTimer (doubles, milliseconds)
    std::size_t timerNextDeadline;
    std::size_t timerStep;

    // cScene / cCamera3D
    std::size_t sceneActiveCamera;
    std::size_t cameraPosition;
    std::size_t cameraPitch;
    std::size_t cameraYaw;
    std::size_t cameraRoll;
    std::size_t cameraPitchHigh;
    std::size_t cameraPitchLow;
    std::size_t cameraYawHigh;
    std::size_t cameraYawLow;
    std::size_t cameraRotateMode;
    std::size_t cameraGetTypeVtableIndex;
    std::uint32_t cameraEulerAnglesMode;
    std::int32_t cameraType3D;

    // cNode3D / iEntity3D / cMeshEntity
    std::size_t nodeLocalMatrix;
    std::size_t entityLocalMatrix;
    std::size_t meshGetModelMatrixVtableIndex;
    std::size_t meshModelMatrixCache;
    std::size_t updateGraphicsVtableIndex;
    std::size_t meshBoneMatricesBegin;
    std::size_t meshBoneMatricesEnd;
    std::size_t meshBoneMatricesCapacity;
    std::size_t meshSkeletonPhysicsActive;
    std::size_t meshSkeletonPhysicsSleeping;

    // cSubMeshEntity
    std::size_t subMeshOwner;
    std::size_t subMeshDynamicVertexBuffer;
    std::size_t subMeshGraphicsUpdated;

    // iParticleEmitter3D, relative to the iRenderable sub-object the
    // UpdateGraphics hook receives.
    std::ptrdiff_t emitterParticlesBegin;
    std::ptrdiff_t emitterParticlesEnd;
    std::ptrdiff_t emitterParticlesCapacity;
    std::ptrdiff_t emitterActiveCount;
    std::ptrdiff_t emitterGraphicsDirty;
    std::ptrdiff_t emitterDrawType;
    std::ptrdiff_t emitterType;
    std::size_t particlePosition;
    std::size_t particleLastPosition;
    std::uint32_t emitterLineDrawType;
    std::uint32_t emitterBeamType;

    // cRendererPostEffects: image trail and motion blur
    std::size_t imageTrailActive;
    std::size_t imageTrailFirstPass;
    std::size_t imageTrailAmount;
    std::size_t motionBlurVertexProgram;
    std::size_t motionBlurFragmentProgram;
    std::size_t motionBlurActive;
    std::size_t motionBlurScale;
    std::size_t motionBlurFirstPass;

    // cPlayerHands / iHudModel
    std::size_t handsCurrentModels;
    std::size_t handsCurrentModelCount;
    std::int32_t handsExpectedModelCount;
    std::size_t hudModelEntity;

    // cButtonHandler
    std::size_t handlerSensitivity;
    std::size_t handlerInvertY;
    std::size_t handlerInit;
    std::size_t handlerPlayer;
    std::size_t handlerState;
    std::int32_t handlerStateGame;
    std::int32_t handlerStateMainMenu;

    // cInit
    std::size_t initHasHaptics;     // kAbsent when the game has no haptic path
    std::size_t initPlayer;
    std::size_t initInventory;
    std::size_t initNotebook;
    std::size_t initNumericalPanel;
    std::size_t initDeathMenu;
    std::size_t initMainMenu;
    std::size_t initGameGui;        // kAbsent when the game has no in-game GUI owner

    // UI owners
    std::size_t inventoryActive;
    std::size_t inventoryMessageActive;
    std::size_t notebookActive;
    std::size_t numericalPanelActive;
    std::size_t deathMenuActive;
    std::size_t mainMenuActionText;
    std::size_t gameGuiActiveSet;   // non-null pointer while the GUI owns input
    std::size_t mainMenuCursor;
    std::size_t deathMenuCursor;
    std::size_t numericalPanelCursor;
    std::size_t notebookCursor;
    std::size_t inventoryCursor;

    // cPlayer
    std::size_t playerActive;
    std::size_t playerCamera;
    std::size_t playerLookSpeed;
    std::size_t playerDeath;
    std::size_t playerDeathActive;
    std::size_t playerState;
    std::size_t playerCrossHairState;
    std::size_t playerCrossHairPosition;
    std::int32_t playerStateNormal;
    std::int32_t playerStateCount;
    std::int32_t crossHairStateCount;
    // Bit n set: player state n moves the interaction cursor with the mouse.
    std::uint32_t interactionCursorStates;

    // cMouseSDL / iLowLevelGraphics
    std::size_t mouseLowLevelGraphics;
    std::size_t graphicsGetScreenSizeVtableIndex;
};

// The layout selected for this process. Valid only after SelectLayout.
[[nodiscard]] const Layout& ActiveLayout() noexcept;
[[nodiscard]] bool HasActiveLayout() noexcept;

// Selects the layout once. A second call with a different table is refused so
// no adapter can read through another adapter's layout.
[[nodiscard]] bool SelectLayout(const Layout& layout) noexcept;

} // namespace pbpfps::target
