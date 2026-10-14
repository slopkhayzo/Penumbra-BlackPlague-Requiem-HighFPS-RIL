#include "telemetry.h"

#include "camera_runtime.h"
#include "entity_root_runtime.h"
#include "frame_cadence.h"
#include "gui_cursor_runtime.h"
#include "hands_runtime.h"
#include "image_trail_runtime.h"
#include "interaction_cursor_runtime.h"
#include "logger.h"
#include "mesh_cache.h"
#include "motion_blur_runtime.h"
#include "mouse_preview_math.h"
#include "particle_runtime.h"
#include "pose_runtime.h"
#include "presentation_clock.h"
#include "target_layout.h"
#include "world_runtime.h"

#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace pbpfps::telemetry {
namespace {

enum class EventKind : std::uint8_t {
    FixedUpdate,
    RenderList,
    OnDraw,
    SceneRender,
    PhysicsStep,
    Swap,
    PostSwap,
};

struct Counters {
    std::atomic<std::uint64_t> fixedUpdates{0};
    std::atomic<std::uint64_t> renderLists{0};
    std::atomic<std::uint64_t> onDraws{0};
    std::atomic<std::uint64_t> sceneRenders{0};
    std::atomic<std::uint64_t> physicsSteps{0};
    std::atomic<std::uint64_t> pollEventCalls{0};
    std::atomic<std::uint64_t> pollEventsReturned{0};
    std::atomic<std::uint64_t> relativeMouseCalls{0};
    std::atomic<std::uint64_t> swaps{0};
    std::atomic<std::uint64_t> postSwaps{0};
};

struct EventSample {
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<DWORD> threadId{0};
    std::atomic<std::uint8_t> kind{0};
};

constexpr std::size_t kSampleCapacity = 96;
Counters g_counters;
std::array<EventSample, kSampleCapacity> g_samples{};
std::atomic<std::uint64_t> g_sequence{0};
std::atomic<std::uint32_t> g_sampleCursor{0};
std::atomic<ULONGLONG> g_lastReportMs{0};
DWORD g_reportIntervalMs = 5000;
bool g_installed = false;
bool g_telemetryEnabled = false;
bool g_renderUnlockEnabled = false;
bool g_cameraInterpolationEnabled = false;
bool g_handsRootInterpolationEnabled = false;
bool g_worldRootInterpolationEnabled = false;
bool g_handsPoseInterpolationEnabled = false;
bool g_worldPoseInterpolationEnabled = false;
bool g_ragdollPoseInterpolationEnabled = false;
bool g_particleInterpolationEnabled = false;
bool g_imageTrailTimingEnabled = false;
bool g_motionBlurTimingEnabled = false;
bool g_mouseOverlayEnabled = false;
bool g_mouseAxisCorrectionEnabled = false;
bool g_interactionCursorInterpolationEnabled = false;
bool g_guiMouseOverlayEnabled = false;
thread_local bool g_insideNewtonUpdate = false;
thread_local bool g_insideUpdaterUpdate = false;
thread_local bool g_insideMouseSdlUpdate = false;
thread_local bool g_mouseAxisCorrectionArmed = false;
thread_local float g_mouseYawScale = 1.0F;
thread_local bool g_insidePlayerHandsUpdate = false;
thread_local void* g_currentPlayerHands = nullptr;
cadence::FrameCadence g_swapCadence;
std::int64_t g_qpcFrequency = 0;
double g_updateStepSum = 0.0;
double g_renderFrameTimeSum = 0.0;
float g_renderFrameTimeMin = 0.0F;
float g_renderFrameTimeMax = 0.0F;
std::uint64_t g_updateStepSamples = 0;
std::uint64_t g_renderFrameTimeSamples = 0;
double g_physicsStepSum = 0.0;
std::uint64_t g_physicsStepSamples = 0;
clock::PresentationClock g_presentationClock{};
void* g_logicTimer = nullptr;
std::uint64_t g_tickSerial = 0;
camera::Runtime g_cameraRuntime{};
constexpr std::size_t kCameraResultCount =
    static_cast<std::size_t>(camera::FrameResult::restoreFailed) + 1U;
std::array<std::uint64_t, kCameraResultCount> g_cameraResults{};
interaction_cursor::Runtime g_interactionCursorRuntime{};
constexpr std::size_t kInteractionCursorResultCount =
    static_cast<std::size_t>(
        interaction_cursor::FrameResult::restoreFailed) + 1U;
std::array<std::uint64_t, kInteractionCursorResultCount>
    g_interactionCursorResults{};
gui_cursor::Runtime g_guiCursorRuntime{};
constexpr std::size_t kGuiCursorResultCount =
    static_cast<std::size_t>(gui_cursor::FrameResult::restoreFailed) + 1U;
std::array<std::uint64_t, kGuiCursorResultCount> g_guiCursorResults{};
std::array<std::uint64_t, 5> g_guiCursorOwners{};
hands::Runtime g_handsRuntime{};
hands::BatchResult g_handsStats{};
world::Runtime g_worldRuntime{};
world::BatchResult g_worldStats{};
mesh::Cache g_meshCache{};
mesh::RefreshResult g_meshStats{};
std::uint64_t g_worldStaged = 0;
std::uint64_t g_worldNotifyInvalid = 0;
std::uint64_t g_worldCapacity = 0;
std::uint64_t g_meshCollected = 0;
std::uint64_t g_meshDuplicates = 0;
std::uint64_t g_meshCapacity = 0;
std::uint64_t g_handsStaged = 0;
std::uint64_t g_handsIgnored = 0;
std::uint64_t g_handsNotifyInvalid = 0;
std::uint64_t g_handsCapacity = 0;
pose::Runtime g_poseRuntime{};
pose::BatchResult g_poseStats{};
std::uint64_t g_poseCaptured = 0;
std::uint64_t g_poseUnchanged = 0;
std::uint64_t g_poseIgnored = 0;
std::uint64_t g_poseHistoryReset = 0;
std::uint64_t g_poseInvalid = 0;
std::uint64_t g_poseCapacity = 0;
std::uint64_t g_poseSubMeshes = 0;
std::uint64_t g_poseSubMeshDuplicates = 0;
std::uint64_t g_poseSubMeshInvalid = 0;
std::uint64_t g_poseSubMeshCapacity = 0;
entity_root::Runtime g_entityRootRuntime{};
entity_root::BatchResult g_entityRootStats{};
std::uint64_t g_entityRootStaged = 0;
std::uint64_t g_entityRootIgnored = 0;
std::uint64_t g_entityRootNotifyInvalid = 0;
std::uint64_t g_entityRootCapacity = 0;
particle::Runtime g_particleRuntime{};
particle::BatchResult g_particleStats{};
std::uint64_t g_particleCollected = 0;
std::uint64_t g_particleDuplicates = 0;
std::uint64_t g_particleLines = 0;
std::uint64_t g_particleBeamsSkipped = 0;
std::uint64_t g_particleInvalid = 0;
std::uint64_t g_particleCapacity = 0;
image_trail::Runtime g_imageTrailRuntime{};
double g_lastImageTrailWallTime = 0.0;
motion_blur::Runtime g_motionBlurRuntime{};
double g_lastMotionBlurWallTime = 0.0;

struct MouseStats {
    std::uint64_t mouseSdlUpdates = 0;
    std::uint64_t buttonHandlerUpdates = 0;
    std::uint64_t presentationPolls = 0;
    std::uint64_t nonzeroPolls = 0;
    std::uint64_t acquiredCounts = 0;
    std::uint64_t deferredFreshCounts = 0;
    std::uint64_t fixedDeliveries = 0;
    std::uint64_t deliveredCounts = 0;
    std::uint64_t immediateFallbacks = 0;
    std::uint64_t ineligibleFrames = 0;
    std::uint64_t previewApplied = 0;
    std::uint64_t previewUnavailable = 0;
    std::uint64_t handsCorrectionBuilt = 0;
    std::uint64_t handsCorrectionApplied = 0;
    std::uint64_t handsCorrectionFailed = 0;
    std::uint64_t overflows = 0;
    std::uint64_t yawCorrections = 0;
    std::uint64_t yawScaleUnavailable = 0;
    double yawScaleSum = 0.0;
    std::array<std::uint64_t, 13> eligibilityRejects{};
};

MouseStats g_mouseStats{};
mouse::RawCounts g_pendingMouseCounts{};
mouse::RawCounts g_deliveredMouseCounts{};
bool g_deliveryAwaitingButtonHandler = false;
// The game may run cButtonHandler::Update more than once per logic tick.
// Reading the relative mouse position clears it, so only the first run turns
// the camera and the preview needs no scaling; the delivery is still measured
// across all runs so a later run can never be mistaken for the whole tick.
bool g_deliveryCapturing = false;
void* g_deliveryCapturePlayer = nullptr;
void* g_deliveryCaptureCamera = nullptr;
float g_deliveryCapturePitch = 0.0F;
float g_deliveryCaptureYaw = 0.0F;
std::uint32_t g_handlerRunsThisTick = 0;
std::uint32_t g_handlerRunsPerTick = 1;
mouse::AngularDelta g_deliveryCandidate{};
bool g_deliveryCandidateReady = false;
mouse::AngularDelta g_deliveryBridge{};
std::uint64_t g_deliveryBridgeSerial = 0;
std::uint64_t g_deliveryBridgeGeneration = 0;
bool g_deliveryBridgeValid = false;
void* g_mouseSdlObject = nullptr;
void* g_buttonHandler = nullptr;

struct ImageTrailStats {
    std::uint64_t calls = 0;
    std::uint64_t inactive = 0;
    std::uint64_t firstPass = 0;
    std::uint64_t applied = 0;
    std::uint64_t invalid = 0;
    std::uint64_t reentrant = 0;
    std::uint64_t restored = 0;
    std::uint64_t restoreFailed = 0;
    double frameTimeSum = 0.0;
    double stockAmountSum = 0.0;
    double correctedAmountSum = 0.0;
};

ImageTrailStats g_imageTrailStats{};

struct MotionBlurStats {
    std::uint64_t calls = 0;
    std::uint64_t inactive = 0;
    std::uint64_t unavailable = 0;
    std::uint64_t firstPass = 0;
    std::uint64_t applied = 0;
    std::uint64_t invalid = 0;
    std::uint64_t reentrant = 0;
    std::uint64_t restored = 0;
    std::uint64_t restoreFailed = 0;
    double frameTimeSum = 0.0;
    double stockScaleSum = 0.0;
    double correctedScaleSum = 0.0;
};

MotionBlurStats g_motionBlurStats{};

struct PhaseStats {
    std::uint64_t interpolated = 0;
    std::uint64_t unavailable = 0;
    std::uint64_t resets = 0;
    std::uint64_t invalidTimer = 0;
    std::uint64_t monotonicViolations = 0;
    std::uint64_t timerResets = 0;
    double alphaSum = 0.0;
    double alphaMin = 0.0;
    double alphaMax = 0.0;
    clock::ResetReason lastResetReason = clock::ResetReason::none;
};

PhaseStats g_phaseStats{};
bool g_hasLastPhase = false;
std::uint64_t g_lastPhaseGeneration = 0;
std::uint64_t g_lastPhaseNewerSerial = 0;
double g_lastAlpha = 0.0;
clock::Sample g_drawPhase{};
bool g_drawPhaseReady = false;
bool g_drawPhaseTimerValid = false;

using UpdaterUpdateFn = void(__thiscall*)(void*, float);
using GameRunFn = void(__thiscall*)(void*);
using LogicTimerResetFn = void(__thiscall*)(void*);
using UpdaterNoArgFn = void(__thiscall*)(void*);
using SceneUpdateRenderListFn = void(__thiscall*)(void*, float);
using SceneRenderFn = void(__thiscall*)(void*, void*, float);
using SdlGlSwapBuffersFn = void(__cdecl*)();
using SdlGetTicksFn = std::uint32_t(__cdecl*)();
using NewtonUpdateFn = void(__cdecl*)(void*, float);
using SdlPollEventFn = int(__cdecl*)(void*);
using SdlGetRelativeMouseStateFn = std::uint8_t(__cdecl*)(int*, int*);
using SdlPumpEventsFn = void(__cdecl*)();
using MouseSdlUpdateFn = void(__thiscall*)(void*);
using ButtonHandlerUpdateFn = void(__thiscall*)(void*, float);
using PlayerAddYawFn = void(__thiscall*)(void*, float);
using NodeSetMatrixFn =
    void(__thiscall*)(void*, const transform::Matrix&, bool);
using PlayerHandsUpdateFn = void(__thiscall*)(void*, float);
using EntitySetMatrixFn =
    void(__thiscall*)(void*, const transform::Matrix&);
using MeshGetModelMatrixFn =
    transform::Matrix*(__thiscall*)(void*, void*);
using UpdateGraphicsFn = void(__thiscall*)(void*, void*, float, void*);
using ImageTrailRenderFn = void(__thiscall*)(void*);

UpdaterUpdateFn g_updaterUpdate = nullptr;
GameRunFn g_gameRun = nullptr;
LogicTimerResetFn g_logicTimerReset = nullptr;
UpdaterNoArgFn g_updaterOnDraw = nullptr;
UpdaterNoArgFn g_updaterOnPostBufferSwap = nullptr;
SceneUpdateRenderListFn g_sceneUpdateRenderList = nullptr;
SceneRenderFn g_sceneRender = nullptr;
SdlGlSwapBuffersFn g_sdlGlSwapBuffers = nullptr;
SdlGetTicksFn g_sdlGetTicks = nullptr;
NewtonUpdateFn g_newtonUpdate = nullptr;
SdlPollEventFn g_sdlPollEvent = nullptr;
SdlGetRelativeMouseStateFn g_sdlGetRelativeMouseState = nullptr;
SdlPumpEventsFn g_sdlPumpEvents = nullptr;
MouseSdlUpdateFn g_mouseSdlUpdate = nullptr;
ButtonHandlerUpdateFn g_buttonHandlerUpdate = nullptr;
PlayerAddYawFn g_playerAddYaw = nullptr;
NodeSetMatrixFn g_nodeSetMatrix = nullptr;
PlayerHandsUpdateFn g_playerHandsUpdate = nullptr;
EntitySetMatrixFn g_entitySetMatrix = nullptr;
MeshGetModelMatrixFn g_meshGetModelMatrix = nullptr;
UpdateGraphicsFn g_meshUpdateGraphics = nullptr;
UpdateGraphicsFn g_subMeshUpdateGraphics = nullptr;
UpdateGraphicsFn g_particleUpdateGraphics = nullptr;
ImageTrailRenderFn g_imageTrailRender = nullptr;
ImageTrailRenderFn g_motionBlurRender = nullptr;

// Wall-clock cost of the plugin's own work around each main scene render.
// The engine render between the prepare and finish halves is excluded.
struct OverheadStat {
    std::int64_t sum = 0;
    std::int64_t max = 0;

    void Add(const std::int64_t ticks) noexcept {
        sum += ticks;
        max = (std::max)(max, ticks);
    }
};

struct OverheadStats {
    OverheadStat prepare{};
    OverheadStat finish{};
    OverheadStat total{};
    OverheadStat world{};
    OverheadStat particles{};
    OverheadStat pose{};
    OverheadStat meshCache{};
    std::uint64_t frames = 0;
    // Whole-call durations, engine work included, so a run with every
    // feature switched off gives the stock baseline for the same scene.
    OverheadStat fixedUpdate{};
    OverheadStat renderList{};
    OverheadStat sceneRender{};
    std::uint64_t fixedUpdates = 0;
    std::uint64_t renderLists = 0;
};

OverheadStats g_overheadStats{};

std::int64_t OverheadNow() noexcept {
    LARGE_INTEGER counter{};
    if (!g_telemetryEnabled || g_qpcFrequency <= 0 ||
        !QueryPerformanceCounter(&counter)) {
        return 0;
    }
    return counter.QuadPart;
}

bool PoseInterpolationEnabled() noexcept {
    return g_handsPoseInterpolationEnabled ||
           g_worldPoseInterpolationEnabled ||
           g_ragdollPoseInterpolationEnabled;
}

double WallTimeSeconds() noexcept {
    LARGE_INTEGER counter{};
    if (g_qpcFrequency <= 0 || !QueryPerformanceCounter(&counter)) {
        return static_cast<double>(GetTickCount64()) / 1000.0;
    }
    return static_cast<double>(counter.QuadPart) /
           static_cast<double>(g_qpcFrequency);
}

template <typename T>
T ReadObject(const void* object, const std::size_t offset) noexcept {
    T value{};
    if (object != nullptr) {
        std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset,
                    sizeof(value));
    }
    return value;
}

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }

void* CurrentPlayer() noexcept {
    return ReadObject<void*>(g_buttonHandler, Fields().handlerPlayer);
}

clock::Sample EvaluatePresentationPhase(bool& timerPhaseValid) noexcept {
    double alpha = 0.0;
    double rawAlpha = 0.0;
    timerPhaseValid = false;
    if (g_logicTimer != nullptr && g_sdlGetTicks != nullptr) {
        double nextTickMilliseconds = 0.0;
        double stepMilliseconds = 0.0;
        const auto* const timer = static_cast<const std::uint8_t*>(g_logicTimer);
        std::memcpy(&nextTickMilliseconds, timer + Fields().timerNextDeadline,
                    sizeof(nextTickMilliseconds));
        std::memcpy(&stepMilliseconds, timer + Fields().timerStep,
                    sizeof(stepMilliseconds));
        timerPhaseValid = clock::LogicTimerAlpha(
            nextTickMilliseconds, stepMilliseconds, 1.0,
            static_cast<double>(g_sdlGetTicks()), 0.0, alpha, &rawAlpha);
    }
    return g_presentationClock.Evaluate(WallTimeSeconds(), alpha,
                                        timerPhaseValid);
}

std::uint64_t CountMagnitude(const mouse::RawCounts counts) noexcept {
    const auto magnitude = [](const std::int64_t value) noexcept {
        return static_cast<std::uint64_t>(value < 0 ? -value : value);
    };
    return magnitude(counts.x) + magnitude(counts.y);
}

struct MouseContext {
    void* player = nullptr;
    void* camera = nullptr;
    float sensitivity = 0.0F;
    float lookSpeed = 0.0F;
    bool invertY = false;
};

struct GuiMouseContext {
    void* owner = nullptr;
    void* player = nullptr;
    gui_cursor::OwnerKind kind = gui_cursor::OwnerKind::mainMenu;
    float sensitivity = 0.0F;
};

enum class MouseEligibility : std::size_t {
    eligible,
    overlayDisabled,
    noButtonHandler,
    noMouseObject,
    buttonState,
    noInit,
    uiOwned,
    haptics,
    noPlayer,
    inactivePlayer,
    deadPlayer,
    playerState,
    noCamera,
    invalidScale,
};

constexpr std::size_t MouseRejectIndex(const MouseEligibility eligibility) {
    return static_cast<std::size_t>(eligibility) - 1;
}

// True while the in-game engine-GUI owner (absent in some targets) holds the
// mouse. It consumes absolute mouse positions, so neither the look preview
// nor the software-cursor overlay may run under it.
bool GameGuiOwnsMouse(void* const init) noexcept {
    const target::Layout& fields = Fields();
    if (fields.initGameGui == target::kAbsent) {
        return false;
    }
    void* const gameGui = ReadObject<void*>(init, fields.initGameGui);
    return ReadObject<void*>(gameGui, fields.gameGuiActiveSet) != nullptr;
}

MouseEligibility ReadEligibleMouseContext(MouseContext& context) noexcept {
    context = {};
    if (!g_mouseOverlayEnabled && !g_mouseAxisCorrectionEnabled) {
        return MouseEligibility::overlayDisabled;
    }
    if (g_buttonHandler == nullptr) {
        return MouseEligibility::noButtonHandler;
    }
    if (g_mouseSdlObject == nullptr) {
        return MouseEligibility::noMouseObject;
    }
    const target::Layout& fields = Fields();
    if (ReadObject<std::int32_t>(g_buttonHandler, fields.handlerState) !=
        fields.handlerStateGame) {
        return MouseEligibility::buttonState;
    }
    void* const init = ReadObject<void*>(g_buttonHandler, fields.handlerInit);
    if (init == nullptr) {
        return MouseEligibility::noInit;
    }
    void* const inventory = ReadObject<void*>(init, fields.initInventory);
    void* const notebook = ReadObject<void*>(init, fields.initNotebook);
    void* const numericalPanel =
        ReadObject<void*>(init, fields.initNumericalPanel);
    void* const deathMenu = ReadObject<void*>(init, fields.initDeathMenu);
    if (ReadObject<std::uint8_t>(inventory, fields.inventoryActive) != 0 ||
        ReadObject<std::uint8_t>(notebook, fields.notebookActive) != 0 ||
        ReadObject<std::uint8_t>(numericalPanel,
                                 fields.numericalPanelActive) != 0 ||
        ReadObject<std::uint8_t>(deathMenu, fields.deathMenuActive) != 0 ||
        GameGuiOwnsMouse(init)) {
        return MouseEligibility::uiOwned;
    }
    if (fields.initHasHaptics != target::kAbsent &&
        ReadObject<std::uint8_t>(init, fields.initHasHaptics) != 0) {
        return MouseEligibility::haptics;
    }
    context.player = ReadObject<void*>(g_buttonHandler, fields.handlerPlayer);
    if (context.player == nullptr) {
        return MouseEligibility::noPlayer;
    }
    if (ReadObject<std::uint8_t>(context.player, fields.playerActive) == 0) {
        return MouseEligibility::inactivePlayer;
    }
    void* const death = ReadObject<void*>(context.player, fields.playerDeath);
    if (death != nullptr &&
        ReadObject<std::uint8_t>(death, fields.playerDeathActive) != 0) {
        return MouseEligibility::deadPlayer;
    }
    if (ReadObject<std::int32_t>(context.player, fields.playerState) !=
        fields.playerStateNormal) {
        return MouseEligibility::playerState;
    }
    context.camera = ReadObject<void*>(context.player, fields.playerCamera);
    context.sensitivity =
        ReadObject<float>(g_buttonHandler, fields.handlerSensitivity);
    context.lookSpeed =
        ReadObject<float>(context.player, fields.playerLookSpeed);
    context.invertY =
        ReadObject<std::uint8_t>(g_buttonHandler, fields.handlerInvertY) != 0;
    if (context.camera == nullptr) {
        return MouseEligibility::noCamera;
    }
    if (!std::isfinite(context.sensitivity) ||
        !std::isfinite(context.lookSpeed) || context.sensitivity < 0.0F ||
        context.lookSpeed < 0.0F) {
        return MouseEligibility::invalidScale;
    }
    return MouseEligibility::eligible;
}

bool ReadEligibleGuiMouseContext(GuiMouseContext& context) noexcept {
    context = {};
    if (!g_guiMouseOverlayEnabled || g_buttonHandler == nullptr ||
        g_mouseSdlObject == nullptr) {
        return false;
    }
    const target::Layout& fields = Fields();
    void* const init = ReadObject<void*>(g_buttonHandler, fields.handlerInit);
    if (init == nullptr ||
        (fields.initHasHaptics != target::kAbsent &&
         ReadObject<std::uint8_t>(init, fields.initHasHaptics) != 0)) {
        return false;
    }
    context.sensitivity =
        ReadObject<float>(g_buttonHandler, fields.handlerSensitivity);
    if (!std::isfinite(context.sensitivity) || context.sensitivity < 0.0F) {
        return false;
    }
    const std::int32_t state =
        ReadObject<std::int32_t>(g_buttonHandler, fields.handlerState);
    if (state == fields.handlerStateMainMenu) {
        context.owner = ReadObject<void*>(init, fields.initMainMenu);
        if (context.owner == nullptr ||
            ReadObject<void*>(context.owner, fields.mainMenuActionText) !=
                nullptr) {
            return false;
        }
        context.kind = gui_cursor::OwnerKind::mainMenu;
        return true;
    }
    if (state != fields.handlerStateGame || GameGuiOwnsMouse(init)) {
        return false;
    }

    void* const deathMenu = ReadObject<void*>(init, fields.initDeathMenu);
    void* const numericalPanel =
        ReadObject<void*>(init, fields.initNumericalPanel);
    void* const notebook = ReadObject<void*>(init, fields.initNotebook);
    void* const inventory = ReadObject<void*>(init, fields.initInventory);
    if (ReadObject<std::uint8_t>(deathMenu, fields.deathMenuActive) != 0) {
        context.owner = deathMenu;
        context.kind = gui_cursor::OwnerKind::deathMenu;
    } else if (ReadObject<std::uint8_t>(numericalPanel,
                                        fields.numericalPanelActive) != 0) {
        context.owner = numericalPanel;
        context.kind = gui_cursor::OwnerKind::numericalPanel;
    } else if (ReadObject<std::uint8_t>(notebook, fields.notebookActive) !=
               0) {
        context.owner = notebook;
        context.kind = gui_cursor::OwnerKind::notebook;
    } else if (ReadObject<std::uint8_t>(inventory, fields.inventoryActive) !=
                   0 &&
               ReadObject<std::uint8_t>(inventory,
                                        fields.inventoryMessageActive) == 0) {
        context.owner = inventory;
        context.kind = gui_cursor::OwnerKind::inventory;
    } else {
        return false;
    }
    context.player = ReadObject<void*>(init, fields.initPlayer);
    if (context.player == nullptr) {
        context.player =
            ReadObject<void*>(g_buttonHandler, fields.handlerPlayer);
    }
    return context.owner != nullptr && context.player != nullptr;
}

bool ReadMouseScreenSize(float& width, float& height) noexcept {
    width = 0.0F;
    height = 0.0F;
    void* const graphics =
        ReadObject<void*>(g_mouseSdlObject, Fields().mouseLowLevelGraphics);
    if (graphics == nullptr) {
        return false;
    }
    void** const vtable = ReadObject<void**>(graphics, 0);
    const std::size_t getScreenSizeIndex =
        Fields().graphicsGetScreenSizeVtableIndex;
    if (vtable == nullptr || vtable[getScreenSizeIndex] == nullptr) {
        return false;
    }
    struct Vec2 {
        float x;
        float y;
    } screen{};
    using GetVec2Fn = void*(__thiscall*)(void*, Vec2*);
    const auto getScreenSize = reinterpret_cast<GetVec2Fn>(vtable[getScreenSizeIndex]);
    getScreenSize(graphics, &screen);
    width = screen.x;
    height = screen.y;
    return std::isfinite(width) && std::isfinite(height) && width > 0.0F &&
           height > 0.0F;
}

void PollPresentationMouse() noexcept {
    MouseContext context{};
    const MouseEligibility eligibility = ReadEligibleMouseContext(context);
    if (eligibility != MouseEligibility::eligible) {
        ++g_mouseStats.eligibilityRejects[MouseRejectIndex(eligibility)];
        ++g_mouseStats.ineligibleFrames;
        return;
    }
    if (g_sdlPumpEvents == nullptr ||
        g_sdlGetRelativeMouseState == nullptr) {
        ++g_mouseStats.ineligibleFrames;
        return;
    }
    g_sdlPumpEvents();
    int x = 0;
    int y = 0;
    g_sdlGetRelativeMouseState(&x, &y);
    ++g_mouseStats.presentationPolls;
    if (x == 0 && y == 0) {
        return;
    }
    ++g_mouseStats.nonzeroPolls;
    const mouse::RawCounts acquired{x, y};
    if (!mouse::Accumulate(g_pendingMouseCounts, x, y)) {
        ++g_mouseStats.overflows;
        return;
    }
    g_mouseStats.acquiredCounts += CountMagnitude(acquired);
}

void PollGuiPresentationMouse() noexcept {
    GuiMouseContext context{};
    if (!ReadEligibleGuiMouseContext(context) ||
        g_sdlPumpEvents == nullptr || g_sdlGetRelativeMouseState == nullptr) {
        return;
    }
    g_sdlPumpEvents();
    int x = 0;
    int y = 0;
    g_sdlGetRelativeMouseState(&x, &y);
    ++g_mouseStats.presentationPolls;
    if (x == 0 && y == 0) {
        return;
    }
    ++g_mouseStats.nonzeroPolls;
    const mouse::RawCounts acquired{x, y};
    if (!mouse::Accumulate(g_pendingMouseCounts, x, y)) {
        ++g_mouseStats.overflows;
        return;
    }
    g_mouseStats.acquiredCounts += CountMagnitude(acquired);
}

gui_cursor::FrameResult ApplyGuiMousePreview() noexcept {
    GuiMouseContext context{};
    float width = 0.0F;
    float height = 0.0F;
    if (!ReadEligibleGuiMouseContext(context)) {
        return gui_cursor::FrameResult::noOwner;
    }
    if (!ReadMouseScreenSize(width, height)) {
        return gui_cursor::FrameResult::invalidInput;
    }
    ++g_guiCursorOwners[static_cast<std::size_t>(context.kind)];
    return g_guiCursorRuntime.Apply(
        context.owner, context.kind, context.player, g_pendingMouseCounts,
        context.sensitivity, width, height);
}

transform::Matrix CameraWorldMatrix(const camera::Pose& pose) noexcept {
    const float cp = std::cos(pose.pitch);
    const float sp = std::sin(pose.pitch);
    const float cy = std::cos(pose.yaw);
    const float sy = std::sin(pose.yaw);
    const float cr = std::cos(pose.roll);
    const float sr = std::sin(pose.roll);
    transform::Matrix result = transform::Identity();
    result.values[0] = cy * cr + sy * sp * sr;
    result.values[1] = -cy * sr + sy * sp * cr;
    result.values[2] = sy * cp;
    result.values[4] = cp * sr;
    result.values[5] = cp * cr;
    result.values[6] = -sp;
    result.values[8] = -sy * cr + cy * sp * sr;
    result.values[9] = sy * sr + cy * sp * cr;
    result.values[10] = cy * cp;
    result.values[3] = pose.position.x;
    result.values[7] = pose.position.y;
    result.values[11] = pose.position.z;
    return result;
}

bool BuildCameraPresentationCorrection(
    const camera::Pose& before, const camera::Pose& after,
    transform::Matrix& correction) noexcept {
    const transform::Matrix oldWorld = CameraWorldMatrix(before);
    const transform::Matrix newWorld = CameraWorldMatrix(after);
    transform::Matrix inverseOld{};
    return transform::InverseRigid(oldWorld, inverseOld) &&
           transform::Multiply(newWorld, inverseOld, correction) &&
           transform::IsRigid(correction);
}

bool ApplyMousePreview(const clock::Sample& phase,
                       transform::Matrix& handsCorrection) noexcept {
    handsCorrection = transform::Identity();
    MouseContext context{};
    float width = 0.0F;
    float height = 0.0F;
    mouse::AngularDelta pending{};
    if (ReadEligibleMouseContext(context) != MouseEligibility::eligible ||
        !ReadMouseScreenSize(width, height) ||
        !mouse::NormalLookAngles(g_pendingMouseCounts, context.sensitivity,
                                 context.lookSpeed, width, height,
                                 context.invertY,
                                 g_mouseAxisCorrectionEnabled, pending)) {
        ++g_mouseStats.previewUnavailable;
        return false;
    }
    mouse::AngularDelta residual{};
    if (g_deliveryBridgeValid) {
        const bool bridgeApplies = mouse::ResidualForSample(
            g_deliveryBridge, g_deliveryBridgeSerial,
            g_deliveryBridgeGeneration, phase, residual);
        if (!bridgeApplies) {
            residual = {};
        }
    }
    camera::Pose before{};
    camera::Pose after{};
    if (!g_cameraRuntime.ApplyAngularPreview(
            pending.pitch + residual.pitch, pending.yaw + residual.yaw,
            &before, &after)) {
        ++g_mouseStats.previewUnavailable;
        return false;
    }
    ++g_mouseStats.previewApplied;
    if (!BuildCameraPresentationCorrection(before, after, handsCorrection)) {
        ++g_mouseStats.handsCorrectionFailed;
        return false;
    }
    ++g_mouseStats.handsCorrectionBuilt;
    return true;
}

const wchar_t* EventName(const EventKind kind) noexcept {
    switch (kind) {
    case EventKind::FixedUpdate:
        return L"update";
    case EventKind::RenderList:
        return L"render-list";
    case EventKind::OnDraw:
        return L"draw";
    case EventKind::SceneRender:
        return L"render";
    case EventKind::PhysicsStep:
        return L"physics";
    case EventKind::Swap:
        return L"swap";
    case EventKind::PostSwap:
        return L"post-swap";
    }
    return L"unknown";
}

void Record(const EventKind kind) noexcept {
    const std::uint64_t sequence =
        g_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint32_t index =
        g_sampleCursor.fetch_add(1, std::memory_order_relaxed);
    if (index >= kSampleCapacity) {
        return;
    }
    EventSample& sample = g_samples[index];
    sample.threadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    sample.kind.store(static_cast<std::uint8_t>(kind), std::memory_order_relaxed);
    sample.sequence.store(sequence, std::memory_order_release);
}

std::uint64_t Take(std::atomic<std::uint64_t>& counter) noexcept {
    return counter.exchange(0, std::memory_order_relaxed);
}

void AddWorldBatch(const world::BatchResult& batch) noexcept {
    g_worldStats.captured += batch.captured;
    g_worldStats.reset += batch.reset;
    g_worldStats.invalid += batch.invalid;
    g_worldStats.ready += batch.ready;
    g_worldStats.pairMiss += batch.pairMiss;
    g_worldStats.applyFailed += batch.applyFailed;
    g_worldStats.restoreFailed += batch.restoreFailed;
}

void AddHandsBatch(const hands::BatchResult& batch) noexcept {
    g_handsStats.captured += batch.captured;
    g_handsStats.reset += batch.reset;
    g_handsStats.invalid += batch.invalid;
    g_handsStats.ready += batch.ready;
    g_handsStats.pairMiss += batch.pairMiss;
    g_handsStats.applyFailed += batch.applyFailed;
    g_handsStats.restoreFailed += batch.restoreFailed;
}

void AddMeshRefresh(const mesh::RefreshResult& refresh) noexcept {
    g_meshStats.refreshed += refresh.refreshed;
    g_meshStats.invalid += refresh.invalid;
}

void AddPoseBatch(const pose::BatchResult& batch) noexcept {
    g_poseStats.targets += batch.targets;
    g_poseStats.applied += batch.applied;
    g_poseStats.regenerated += batch.regenerated;
    g_poseStats.clockWait += batch.clockWait;
    g_poseStats.pairMissing += batch.pairMissing;
    g_poseStats.stationarySkipped += batch.stationarySkipped;
    g_poseStats.invalid += batch.invalid;
    g_poseStats.restoreFailed += batch.restoreFailed;
    g_poseStats.ragdollApplied += batch.ragdollApplied;
    g_poseStats.sleepingReskins += batch.sleepingReskins;
}

void AddEntityRootBatch(const entity_root::BatchResult& batch) noexcept {
    g_entityRootStats.captured += batch.captured;
    g_entityRootStats.reset += batch.reset;
    g_entityRootStats.invalid += batch.invalid;
    g_entityRootStats.ready += batch.ready;
    g_entityRootStats.pairMiss += batch.pairMiss;
    g_entityRootStats.applyFailed += batch.applyFailed;
    g_entityRootStats.restoreFailed += batch.restoreFailed;
}

void AddParticleBatch(const particle::BatchResult& batch) noexcept {
    g_particleStats.targets += batch.targets;
    g_particleStats.particles += batch.particles;
    g_particleStats.applied += batch.applied;
    g_particleStats.regenerated += batch.regenerated;
    g_particleStats.clockWait += batch.clockWait;
    g_particleStats.stationarySkipped += batch.stationarySkipped;
    g_particleStats.invalid += batch.invalid;
    g_particleStats.restoreFailed += batch.restoreFailed;
}

void Report(const ULONGLONG now, const ULONGLONG elapsed) {
    const std::uint64_t fixedUpdates = Take(g_counters.fixedUpdates);
    const std::uint64_t renderLists = Take(g_counters.renderLists);
    const std::uint64_t onDraws = Take(g_counters.onDraws);
    const std::uint64_t sceneRenders = Take(g_counters.sceneRenders);
    const std::uint64_t physicsSteps = Take(g_counters.physicsSteps);
    const std::uint64_t pollEventCalls = Take(g_counters.pollEventCalls);
    const std::uint64_t pollEventsReturned = Take(g_counters.pollEventsReturned);
    const std::uint64_t relativeMouseCalls = Take(g_counters.relativeMouseCalls);
    const std::uint64_t swaps = Take(g_counters.swaps);
    const std::uint64_t postSwaps = Take(g_counters.postSwaps);
    const double seconds = static_cast<double>(elapsed) / 1000.0;

    std::wostringstream summary;
    summary << std::fixed << std::setprecision(1)
            << L"Telemetry " << seconds << L"s: update=" << fixedUpdates
            << L" (" << (fixedUpdates / seconds) << L"/s), render-list="
            << renderLists << L", draw=" << onDraws << L", render="
            << sceneRenders << L", swap=" << swaps << L" ("
            << (swaps / seconds) << L"/s), post-swap=" << postSwaps
            << L", physics=" << physicsSteps << L", poll=" << pollEventCalls
            << L" (events=" << pollEventsReturned << L"), relative-mouse="
            << relativeMouseCalls;
    logger::Write(summary.str());

    const cadence::Snapshot cadence = g_swapCadence.TakeSnapshot();
    std::wostringstream timing;
    timing << std::fixed << std::setprecision(3)
           << L"Timing: swap-ms mean=" << cadence.meanMilliseconds
           << L", p50=" << cadence.p50Milliseconds
           << L", p95=" << cadence.p95Milliseconds
           << L", p99=" << cadence.p99Milliseconds
           << L", max=" << cadence.maxMilliseconds
           << L", samples=" << cadence.samples
           << L", long-gaps=" << cadence.longGaps;
    if (g_updateStepSamples != 0) {
        timing << L", update-step-ms="
               << (g_updateStepSum * 1000.0 /
                   static_cast<double>(g_updateStepSamples));
    }
    if (g_renderFrameTimeSamples != 0) {
        timing << L", render-dt-ms mean="
               << (g_renderFrameTimeSum * 1000.0 /
                   static_cast<double>(g_renderFrameTimeSamples))
               << L" min=" << (g_renderFrameTimeMin * 1000.0F)
               << L" max=" << (g_renderFrameTimeMax * 1000.0F);
    }
    if (g_physicsStepSamples != 0) {
        timing << L", physics-step-ms="
               << (g_physicsStepSum * 1000.0 /
                   static_cast<double>(g_physicsStepSamples));
    }
    logger::Write(timing.str());

    std::wostringstream phase;
    phase << std::fixed << std::setprecision(4)
          << L"Presentation phase: valid=" << g_phaseStats.interpolated
          << L", unavailable=" << g_phaseStats.unavailable
          << L", invalid-timer=" << g_phaseStats.invalidTimer
          << L", resets=" << g_phaseStats.resets
          << L", timer-resets=" << g_phaseStats.timerResets
          << L", monotonic-violations=" << g_phaseStats.monotonicViolations;
    if (g_phaseStats.interpolated != 0) {
        phase << L", alpha mean="
              << (g_phaseStats.alphaSum /
                  static_cast<double>(g_phaseStats.interpolated))
              << L" min=" << g_phaseStats.alphaMin
              << L" max=" << g_phaseStats.alphaMax;
    }
    phase << L", last-reset="
          << clock::ResetReasonName(g_phaseStats.lastResetReason);
    logger::Write(phase.str());
    if (g_cameraInterpolationEnabled) {
        std::wostringstream cameraSummary;
        cameraSummary << L"Camera interpolation:";
        for (std::size_t index = 0; index < g_cameraResults.size(); ++index) {
            if (g_cameraResults[index] == 0) {
                continue;
            }
            cameraSummary << L" "
                          << camera::FrameResultName(
                                 static_cast<camera::FrameResult>(index))
                          << L"=" << g_cameraResults[index];
        }
        logger::Write(cameraSummary.str());
    }
    if (g_interactionCursorInterpolationEnabled) {
        std::wostringstream cursorSummary;
        cursorSummary << L"Interaction cursor interpolation:";
        for (std::size_t index = 0;
             index < g_interactionCursorResults.size(); ++index) {
            if (g_interactionCursorResults[index] == 0) {
                continue;
            }
            cursorSummary
                << L" "
                << interaction_cursor::FrameResultName(
                       static_cast<interaction_cursor::FrameResult>(index))
                << L"=" << g_interactionCursorResults[index];
        }
        logger::Write(cursorSummary.str());
    }
    if (g_guiMouseOverlayEnabled) {
        std::wostringstream cursorSummary;
        cursorSummary << L"GUI cursor overlay:";
        for (std::size_t index = 0; index < g_guiCursorResults.size(); ++index) {
            if (g_guiCursorResults[index] == 0) {
                continue;
            }
            cursorSummary
                << L" "
                << gui_cursor::FrameResultName(
                       static_cast<gui_cursor::FrameResult>(index))
                << L"=" << g_guiCursorResults[index];
        }
        cursorSummary << L", owners main-menu=" << g_guiCursorOwners[0]
                      << L" death=" << g_guiCursorOwners[1]
                      << L" numerical=" << g_guiCursorOwners[2]
                      << L" notebook=" << g_guiCursorOwners[3]
                      << L" inventory=" << g_guiCursorOwners[4];
        logger::Write(cursorSummary.str());
    }
    if (g_worldRootInterpolationEnabled) {
        std::wostringstream worldSummary;
        worldSummary << L"World-root interpolation: staged=" << g_worldStaged
                     << L", captured=" << g_worldStats.captured
                     << L", ready=" << g_worldStats.ready
                     << L", history-reset=" << g_worldStats.reset
                     << L", pair-miss=" << g_worldStats.pairMiss
                     << L", invalid=" << g_worldStats.invalid
                     << L", apply-failed=" << g_worldStats.applyFailed
                     << L", restore-failed=" << g_worldStats.restoreFailed
                     << L", notify-invalid=" << g_worldNotifyInvalid
                     << L", capacity=" << g_worldCapacity
                     << L", meshes-collected=" << g_meshCollected
                     << L", mesh-duplicates=" << g_meshDuplicates
                     << L", mesh-refreshed=" << g_meshStats.refreshed
                     << L", mesh-invalid=" << g_meshStats.invalid
                     << L", mesh-capacity=" << g_meshCapacity;
        logger::Write(worldSummary.str());
    }
    if (g_handsRootInterpolationEnabled) {
        std::wostringstream handsSummary;
        handsSummary << L"Hands-root interpolation: staged=" << g_handsStaged
                     << L", captured=" << g_handsStats.captured
                     << L", ready=" << g_handsStats.ready
                     << L", history-reset=" << g_handsStats.reset
                     << L", pair-miss=" << g_handsStats.pairMiss
                     << L", invalid=" << g_handsStats.invalid
                     << L", apply-failed=" << g_handsStats.applyFailed
                     << L", restore-failed=" << g_handsStats.restoreFailed
                     << L", ignored=" << g_handsIgnored
                     << L", notify-invalid=" << g_handsNotifyInvalid
                     << L", capacity=" << g_handsCapacity;
        logger::Write(handsSummary.str());
    }
    if (PoseInterpolationEnabled()) {
        std::wostringstream poseSummary;
        poseSummary << L"Pose interpolation: captured=" << g_poseCaptured
                    << L", unchanged=" << g_poseUnchanged
                    << L", ignored=" << g_poseIgnored
                    << L", history-reset=" << g_poseHistoryReset
                    << L", targets=" << g_poseStats.targets
                    << L", applied=" << g_poseStats.applied
                    << L", regenerated=" << g_poseStats.regenerated
                    << L", clock-wait=" << g_poseStats.clockWait
                    << L", pair-miss=" << g_poseStats.pairMissing
                    << L", stationary-skipped="
                    << g_poseStats.stationarySkipped
                    << L", invalid=" << (g_poseInvalid + g_poseStats.invalid)
                    << L", restore-failed=" << g_poseStats.restoreFailed
                    << L", capacity=" << g_poseCapacity
                    << L", submeshes=" << g_poseSubMeshes
                    << L", submesh-duplicates=" << g_poseSubMeshDuplicates
                    << L", submesh-invalid=" << g_poseSubMeshInvalid
                    << L", submesh-capacity=" << g_poseSubMeshCapacity
                    << L", ragdoll-applied=" << g_poseStats.ragdollApplied
                    << L", sleeping-reskins=" << g_poseStats.sleepingReskins;
        logger::Write(poseSummary.str());
    }
    if (g_worldPoseInterpolationEnabled) {
        std::wostringstream rootSummary;
        rootSummary << L"Gameplay mesh-root interpolation: staged="
                    << g_entityRootStaged
                    << L", captured=" << g_entityRootStats.captured
                    << L", ready=" << g_entityRootStats.ready
                    << L", history-reset=" << g_entityRootStats.reset
                    << L", pair-miss=" << g_entityRootStats.pairMiss
                    << L", invalid=" << g_entityRootStats.invalid
                    << L", apply-failed=" << g_entityRootStats.applyFailed
                    << L", restore-failed="
                    << g_entityRootStats.restoreFailed
                    << L", ignored=" << g_entityRootIgnored
                    << L", notify-invalid=" << g_entityRootNotifyInvalid
                    << L", capacity=" << g_entityRootCapacity;
        logger::Write(rootSummary.str());
    }
    if (g_particleInterpolationEnabled) {
        std::wostringstream particleSummary;
        particleSummary << L"Particle interpolation: collected="
                        << g_particleCollected
                        << L", duplicates=" << g_particleDuplicates
                        << L", line-emitters=" << g_particleLines
                        << L", beam-skipped=" << g_particleBeamsSkipped
                        << L", targets=" << g_particleStats.targets
                        << L", particles=" << g_particleStats.particles
                        << L", applied=" << g_particleStats.applied
                        << L", regenerated=" << g_particleStats.regenerated
                        << L", clock-wait=" << g_particleStats.clockWait
                        << L", stationary-skipped="
                        << g_particleStats.stationarySkipped
                        << L", invalid="
                        << (g_particleInvalid + g_particleStats.invalid)
                        << L", restore-failed="
                        << g_particleStats.restoreFailed
                        << L", capacity=" << g_particleCapacity;
        logger::Write(particleSummary.str());
    }
    if (g_imageTrailTimingEnabled) {
        std::wostringstream trailSummary;
        trailSummary << L"Image-trail timing: calls="
                     << g_imageTrailStats.calls
                     << L", inactive=" << g_imageTrailStats.inactive
                     << L", first-pass=" << g_imageTrailStats.firstPass
                     << L", applied=" << g_imageTrailStats.applied
                     << L", invalid=" << g_imageTrailStats.invalid
                     << L", reentrant=" << g_imageTrailStats.reentrant
                     << L", restored=" << g_imageTrailStats.restored
                     << L", restore-failed="
                     << g_imageTrailStats.restoreFailed;
        if (g_imageTrailStats.applied != 0) {
            const double applied =
                static_cast<double>(g_imageTrailStats.applied);
            trailSummary << std::fixed << std::setprecision(4)
                         << L", dt-ms mean="
                         << (g_imageTrailStats.frameTimeSum * 1000.0 /
                             applied)
                         << L", stock mean="
                         << (g_imageTrailStats.stockAmountSum / applied)
                         << L", corrected mean="
                         << (g_imageTrailStats.correctedAmountSum / applied);
        }
        logger::Write(trailSummary.str());
    }
    if (g_motionBlurTimingEnabled) {
        std::wostringstream blurSummary;
        blurSummary << L"Motion-blur timing: calls="
                    << g_motionBlurStats.calls
                    << L", inactive=" << g_motionBlurStats.inactive
                    << L", unavailable=" << g_motionBlurStats.unavailable
                    << L", first-pass=" << g_motionBlurStats.firstPass
                    << L", applied=" << g_motionBlurStats.applied
                    << L", invalid=" << g_motionBlurStats.invalid
                    << L", reentrant=" << g_motionBlurStats.reentrant
                    << L", restored=" << g_motionBlurStats.restored
                    << L", restore-failed="
                    << g_motionBlurStats.restoreFailed;
        if (g_motionBlurStats.applied != 0) {
            const double applied =
                static_cast<double>(g_motionBlurStats.applied);
            blurSummary << std::fixed << std::setprecision(4)
                        << L", dt-ms mean="
                        << (g_motionBlurStats.frameTimeSum * 1000.0 /
                            applied)
                        << L", stock mean="
                        << (g_motionBlurStats.stockScaleSum / applied)
                        << L", corrected mean="
                        << (g_motionBlurStats.correctedScaleSum / applied);
        }
        logger::Write(blurSummary.str());
    }
    if (g_mouseOverlayEnabled || g_mouseAxisCorrectionEnabled ||
        g_guiMouseOverlayEnabled) {
        std::wostringstream mouseSummary;
        mouseSummary << L"Mouse overlay: mouse-updates="
                     << g_mouseStats.mouseSdlUpdates
                     << L", button-updates="
                     << g_mouseStats.buttonHandlerUpdates
                     << L", presentation-polls="
                     << g_mouseStats.presentationPolls
                     << L", nonzero-polls=" << g_mouseStats.nonzeroPolls
                     << L", acquired-counts=" << g_mouseStats.acquiredCounts
                     << L", deferred-fresh-counts="
                     << g_mouseStats.deferredFreshCounts
                     << L", fixed-deliveries="
                     << g_mouseStats.fixedDeliveries
                     << L", delivered-counts="
                     << g_mouseStats.deliveredCounts
                     << L", pending-x=" << g_pendingMouseCounts.x
                     << L", pending-y=" << g_pendingMouseCounts.y
                     << L", immediate-fallbacks="
                     << g_mouseStats.immediateFallbacks
                     << L", ineligible-frames="
                     << g_mouseStats.ineligibleFrames
                     << L", preview-applied="
                     << g_mouseStats.previewApplied
                     << L", preview-unavailable="
                     << g_mouseStats.previewUnavailable
                     << L", hands-correction-built="
                     << g_mouseStats.handsCorrectionBuilt
                     << L", hands-correction-applied="
                     << g_mouseStats.handsCorrectionApplied
                     << L", hands-correction-failed="
                     << g_mouseStats.handsCorrectionFailed
                     << L", overflows=" << g_mouseStats.overflows
                     << L", handler-runs-per-tick=" << g_handlerRunsPerTick
                     << L", yaw-corrections="
                     << g_mouseStats.yawCorrections
                     << L", yaw-scale-unavailable="
                     << g_mouseStats.yawScaleUnavailable;
        if (g_mouseStats.yawCorrections != 0) {
            mouseSummary << std::fixed << std::setprecision(4)
                         << L", yaw-scale-mean="
                         << (g_mouseStats.yawScaleSum /
                             static_cast<double>(
                                 g_mouseStats.yawCorrections));
        }
        logger::Write(mouseSummary.str());

        std::wostringstream eligibilitySummary;
        eligibilitySummary
            << L"Mouse eligibility rejects: overlay-disabled="
            << g_mouseStats.eligibilityRejects[0]
            << L", no-button-handler=" << g_mouseStats.eligibilityRejects[1]
            << L", no-mouse-object=" << g_mouseStats.eligibilityRejects[2]
            << L", button-state=" << g_mouseStats.eligibilityRejects[3]
            << L", no-init=" << g_mouseStats.eligibilityRejects[4]
            << L", ui-owned=" << g_mouseStats.eligibilityRejects[5]
            << L", haptics=" << g_mouseStats.eligibilityRejects[6]
            << L", no-player=" << g_mouseStats.eligibilityRejects[7]
            << L", inactive-player=" << g_mouseStats.eligibilityRejects[8]
            << L", dead-player=" << g_mouseStats.eligibilityRejects[9]
            << L", player-state=" << g_mouseStats.eligibilityRejects[10]
            << L", no-camera=" << g_mouseStats.eligibilityRejects[11]
            << L", invalid-scale=" << g_mouseStats.eligibilityRejects[12];
        logger::Write(eligibilitySummary.str());
    }
    if (g_overheadStats.frames != 0 && g_qpcFrequency > 0) {
        const double toMilliseconds =
            1000.0 / static_cast<double>(g_qpcFrequency);
        const double frames = static_cast<double>(g_overheadStats.frames);
        const auto write = [&](std::wostringstream& stream,
                               const wchar_t* const name,
                               const OverheadStat& stat) {
            stream << name << L" mean="
                   << (static_cast<double>(stat.sum) * toMilliseconds / frames)
                   << L" max="
                   << (static_cast<double>(stat.max) * toMilliseconds);
        };
        std::wostringstream overheadSummary;
        overheadSummary << std::fixed << std::setprecision(4)
                        << L"Plugin render overhead ms: ";
        write(overheadSummary, L"total", g_overheadStats.total);
        write(overheadSummary, L", prepare", g_overheadStats.prepare);
        write(overheadSummary, L", finish", g_overheadStats.finish);
        write(overheadSummary, L", world-roots", g_overheadStats.world);
        write(overheadSummary, L", particles", g_overheadStats.particles);
        write(overheadSummary, L", pose", g_overheadStats.pose);
        write(overheadSummary, L", mesh-cache", g_overheadStats.meshCache);
        overheadSummary << L", frames=" << g_overheadStats.frames;
        logger::Write(overheadSummary.str());

        const auto writeCalls = [&](std::wostringstream& stream,
                                    const wchar_t* const name,
                                    const OverheadStat& stat,
                                    const std::uint64_t calls) {
            stream << name << L" mean="
                   << (calls == 0 ? 0.0
                                  : static_cast<double>(stat.sum) *
                                        toMilliseconds /
                                        static_cast<double>(calls))
                   << L" max="
                   << (static_cast<double>(stat.max) * toMilliseconds);
        };
        std::wostringstream callSummary;
        callSummary << std::fixed << std::setprecision(4)
                    << L"Engine call ms (including hooks): ";
        writeCalls(callSummary, L"fixed-update", g_overheadStats.fixedUpdate,
                   g_overheadStats.fixedUpdates);
        writeCalls(callSummary, L", render-list", g_overheadStats.renderList,
                   g_overheadStats.renderLists);
        writeCalls(callSummary, L", scene-render",
                   g_overheadStats.sceneRender, g_overheadStats.frames);
        logger::Write(callSummary.str());
    }
    g_overheadStats = {};
    g_updateStepSum = 0.0;
    g_renderFrameTimeSum = 0.0;
    g_renderFrameTimeMin = 0.0F;
    g_renderFrameTimeMax = 0.0F;
    g_updateStepSamples = 0;
    g_renderFrameTimeSamples = 0;
    g_physicsStepSum = 0.0;
    g_physicsStepSamples = 0;
    g_phaseStats = {};
    g_cameraResults.fill(0);
    g_interactionCursorResults.fill(0);
    g_guiCursorResults.fill(0);
    g_guiCursorOwners.fill(0);
    g_worldStats = {};
    g_worldStaged = 0;
    g_worldNotifyInvalid = 0;
    g_worldCapacity = 0;
    g_meshStats = {};
    g_meshCollected = 0;
    g_meshDuplicates = 0;
    g_meshCapacity = 0;
    g_handsStats = {};
    g_handsStaged = 0;
    g_handsIgnored = 0;
    g_handsNotifyInvalid = 0;
    g_handsCapacity = 0;
    g_poseStats = {};
    g_poseCaptured = 0;
    g_poseUnchanged = 0;
    g_poseIgnored = 0;
    g_poseHistoryReset = 0;
    g_poseInvalid = 0;
    g_poseCapacity = 0;
    g_poseSubMeshes = 0;
    g_poseSubMeshDuplicates = 0;
    g_poseSubMeshInvalid = 0;
    g_poseSubMeshCapacity = 0;
    g_entityRootStats = {};
    g_entityRootStaged = 0;
    g_entityRootIgnored = 0;
    g_entityRootNotifyInvalid = 0;
    g_entityRootCapacity = 0;
    g_particleStats = {};
    g_particleCollected = 0;
    g_particleDuplicates = 0;
    g_particleLines = 0;
    g_particleBeamsSkipped = 0;
    g_particleInvalid = 0;
    g_particleCapacity = 0;
    g_imageTrailStats = {};
    g_motionBlurStats = {};
    g_mouseStats = {};

    const std::uint32_t sampleCount =
        g_sampleCursor.exchange(0, std::memory_order_acq_rel);
    const std::size_t available =
        sampleCount < kSampleCapacity ? sampleCount : kSampleCapacity;
    std::wostringstream trace;
    trace << L"Sequence sample (first " << available << L"): ";
    for (std::size_t index = 0; index < available; ++index) {
        EventSample& sample = g_samples[index];
        const std::uint64_t sequence =
            sample.sequence.load(std::memory_order_acquire);
        const auto kind = static_cast<EventKind>(
            sample.kind.load(std::memory_order_relaxed));
        if (index != 0) {
            trace << L", ";
        }
        trace << sequence << L":" << EventName(kind) << L"@"
              << sample.threadId.load(std::memory_order_relaxed);
        sample.sequence.store(0, std::memory_order_relaxed);
    }
    logger::Write(trace.str());
    g_lastReportMs.store(now, std::memory_order_relaxed);
}

void MaybeReport() {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG previous = g_lastReportMs.load(std::memory_order_relaxed);
    const ULONGLONG elapsed = now - previous;
    if (elapsed >= g_reportIntervalMs) {
        Report(now, elapsed);
    }
}

void __fastcall GameRunDetour(void* self, void*) {
    auto* const limitFps = reinterpret_cast<bool*>(
        static_cast<std::uint8_t*>(self) + Fields().gameLimitFps);
    const bool original = *limitFps;
    if (g_renderUnlockEnabled) {
        *limitFps = false;
        logger::Write(L"Render unlock active: cGame::mbLimitFPS forced false for this run.");
    }
    g_gameRun(self);
    *limitFps = original;
}

void __fastcall LogicTimerResetDetour(void* self, void*) {
    g_logicTimerReset(self);
    g_logicTimer = self;
    g_tickSerial = 0;
    g_presentationClock.Reset(clock::ResetReason::explicitDiscontinuity);
    g_hasLastPhase = false;
    g_drawPhaseReady = false;
    g_cameraRuntime.Reset();
    g_interactionCursorRuntime.Reset();
    g_guiCursorRuntime.Reset();
    g_handsRuntime.Reset();
    g_worldRuntime.Reset();
    g_entityRootRuntime.Reset();
    g_poseRuntime.Reset();
    g_particleRuntime.Reset();
    g_meshCache.Reset();
    g_lastMotionBlurWallTime = 0.0;
    g_pendingMouseCounts = {};
    g_deliveredMouseCounts = {};
    g_deliveryAwaitingButtonHandler = false;
    g_deliveryCapturing = false;
    g_handlerRunsThisTick = 0;
    g_handlerRunsPerTick = 1;
    g_deliveryCandidate = {};
    g_deliveryCandidateReady = false;
    g_deliveryBridge = {};
    g_deliveryBridgeValid = false;
    ++g_phaseStats.timerResets;
    g_phaseStats.lastResetReason = clock::ResetReason::explicitDiscontinuity;
}

void __fastcall UpdaterUpdateDetour(void* self, void*, const float step) {
    g_drawPhaseReady = false;
    g_counters.fixedUpdates.fetch_add(1, std::memory_order_relaxed);
    g_updateStepSum += step;
    ++g_updateStepSamples;
    Record(EventKind::FixedUpdate);
    g_insideUpdaterUpdate = true;
    g_handlerRunsThisTick = 0;
    g_deliveryCapturing = false;
    const std::int64_t updateBegin = OverheadNow();
    g_updaterUpdate(self, step);
    const std::int64_t updateEnd = OverheadNow();
    if (updateBegin != 0 && updateEnd != 0) {
        g_overheadStats.fixedUpdate.Add(updateEnd - updateBegin);
        ++g_overheadStats.fixedUpdates;
    }
    g_insideUpdaterUpdate = false;
    g_deliveryCapturing = false;
    if (g_handlerRunsThisTick != 0) {
        g_handlerRunsPerTick = (std::min)(g_handlerRunsThisTick, 8U);
    }
    const clock::PublishResult published =
        g_presentationClock.Publish(++g_tickSerial, WallTimeSeconds());
    if (g_cameraInterpolationEnabled) {
        const camera::FrameResult captured = g_cameraRuntime.CaptureCompletedTick(
            g_tickSerial, g_presentationClock.Generation());
        ++g_cameraResults[static_cast<std::size_t>(captured)];
    }
    if (g_interactionCursorInterpolationEnabled) {
        const interaction_cursor::FrameResult captured =
            g_interactionCursorRuntime.CaptureCompletedTick(
                CurrentPlayer(), g_tickSerial,
                g_presentationClock.Generation());
        ++g_interactionCursorResults[static_cast<std::size_t>(captured)];
    }
    if (g_mouseOverlayEnabled && g_deliveryCandidateReady) {
        g_deliveryBridge = g_deliveryCandidate;
        g_deliveryBridgeSerial = g_tickSerial;
        g_deliveryBridgeGeneration = g_presentationClock.Generation();
        g_deliveryBridgeValid = true;
        g_deliveryCandidate = {};
        g_deliveryCandidateReady = false;
    }
    if (g_worldRootInterpolationEnabled) {
        AddWorldBatch(g_worldRuntime.CommitCompletedTick(
            g_tickSerial, g_presentationClock.Generation()));
    }
    if (g_handsRootInterpolationEnabled) {
        AddHandsBatch(g_handsRuntime.CommitCompletedTick(
            g_tickSerial, g_presentationClock.Generation()));
    }
    if (g_worldPoseInterpolationEnabled) {
        AddEntityRootBatch(g_entityRootRuntime.CommitCompletedTick(
            g_tickSerial, g_presentationClock.Generation()));
    }
    if (published.resetReason != clock::ResetReason::none) {
        ++g_phaseStats.resets;
        g_phaseStats.lastResetReason = published.resetReason;
        g_hasLastPhase = false;
    }
}

void __fastcall UpdaterOnDrawDetour(void* self, void*) {
    g_counters.onDraws.fetch_add(1, std::memory_order_relaxed);
    Record(EventKind::OnDraw);
    bool cursorApplied = false;
    bool guiCursorApplied = false;
    if (g_interactionCursorInterpolationEnabled) {
        g_drawPhase = EvaluatePresentationPhase(g_drawPhaseTimerValid);
        g_drawPhaseReady = true;
        const interaction_cursor::FrameResult result =
            g_interactionCursorRuntime.Apply(CurrentPlayer(), g_drawPhase);
        ++g_interactionCursorResults[static_cast<std::size_t>(result)];
        cursorApplied =
            result == interaction_cursor::FrameResult::applied;
    }
    if (g_guiMouseOverlayEnabled) {
        PollGuiPresentationMouse();
        const gui_cursor::FrameResult result = ApplyGuiMousePreview();
        ++g_guiCursorResults[static_cast<std::size_t>(result)];
        guiCursorApplied = result == gui_cursor::FrameResult::applied;
    }
    g_updaterOnDraw(self);
    if (guiCursorApplied) {
        const gui_cursor::FrameResult restored = g_guiCursorRuntime.Restore();
        ++g_guiCursorResults[static_cast<std::size_t>(restored)];
        if (restored == gui_cursor::FrameResult::restoreFailed) {
            g_guiMouseOverlayEnabled = false;
            logger::Write(
                L"ERROR: GUI cursor restoration failed; GUI cursor overlay "
                L"has been disabled for the rest of this process.");
        }
    }
    if (cursorApplied) {
        const interaction_cursor::FrameResult restored =
            g_interactionCursorRuntime.Restore();
        ++g_interactionCursorResults[static_cast<std::size_t>(restored)];
        if (restored == interaction_cursor::FrameResult::restoreFailed) {
            g_interactionCursorInterpolationEnabled = false;
            logger::Write(
                L"ERROR: interaction cursor restoration failed; interaction "
                L"cursor interpolation has been disabled for the rest of this process.");
        }
    }
}

void __fastcall UpdaterOnPostBufferSwapDetour(void* self, void*) {
    g_counters.postSwaps.fetch_add(1, std::memory_order_relaxed);
    Record(EventKind::PostSwap);
    g_updaterOnPostBufferSwap(self);
}

void __fastcall MouseSdlUpdateDetour(void* self, void*) {
    ++g_mouseStats.mouseSdlUpdates;
    g_mouseSdlObject = self;
    g_insideMouseSdlUpdate = true;
    g_mouseSdlUpdate(self);
    g_insideMouseSdlUpdate = false;
}

void __fastcall ButtonHandlerUpdateDetour(void* self, void*,
                                          const float step) {
    ++g_mouseStats.buttonHandlerUpdates;
    g_buttonHandler = self;
    ++g_handlerRunsThisTick;
    const std::size_t pitchOffset = Fields().cameraPitch;
    const std::size_t yawOffset = Fields().cameraYaw;
    if (g_deliveryAwaitingButtonHandler) {
        // First run after a delivery: remember where the camera started.
        MouseContext before{};
        if (CountMagnitude(g_deliveredMouseCounts) != 0 &&
            ReadEligibleMouseContext(before) == MouseEligibility::eligible) {
            g_deliveryCapturing = true;
            g_deliveryCapturePlayer = before.player;
            g_deliveryCaptureCamera = before.camera;
            g_deliveryCapturePitch =
                ReadObject<float>(before.camera, pitchOffset);
            g_deliveryCaptureYaw = ReadObject<float>(before.camera, yawOffset);
        }
        g_deliveryAwaitingButtonHandler = false;
        g_deliveredMouseCounts = {};
    }

    const bool previousCorrectionArmed = g_mouseAxisCorrectionArmed;
    const float previousYawScale = g_mouseYawScale;
    g_mouseAxisCorrectionArmed = false;
    g_mouseYawScale = 1.0F;
    if (g_mouseAxisCorrectionEnabled) {
        MouseContext correctionContext{};
        float width = 0.0F;
        float height = 0.0F;
        float scale = 1.0F;
        if (ReadEligibleMouseContext(correctionContext) ==
                MouseEligibility::eligible &&
            ReadMouseScreenSize(width, height) &&
            mouse::AxisEqualizationScale(width, height, scale)) {
            g_mouseAxisCorrectionArmed = true;
            g_mouseYawScale = scale;
        } else {
            ++g_mouseStats.yawScaleUnavailable;
        }
    }
    g_buttonHandlerUpdate(self, step);
    g_mouseAxisCorrectionArmed = previousCorrectionArmed;
    g_mouseYawScale = previousYawScale;
    if (g_deliveryCapturing) {
        MouseContext after{};
        if (ReadEligibleMouseContext(after) == MouseEligibility::eligible &&
            after.player == g_deliveryCapturePlayer &&
            after.camera == g_deliveryCaptureCamera) {
            const float pitchAfter =
                ReadObject<float>(after.camera, pitchOffset);
            const float yawAfter = ReadObject<float>(after.camera, yawOffset);
            if (std::isfinite(g_deliveryCapturePitch) &&
                std::isfinite(g_deliveryCaptureYaw) &&
                std::isfinite(pitchAfter) && std::isfinite(yawAfter)) {
                // Overwritten by each later run, so the candidate ends as the
                // whole tick's rotation.
                g_deliveryCandidate = {pitchAfter - g_deliveryCapturePitch,
                                       yawAfter - g_deliveryCaptureYaw};
                g_deliveryCandidateReady = true;
            }
        } else {
            g_deliveryCapturing = false;
            g_deliveryCandidateReady = false;
        }
    }
}

void __fastcall PlayerAddYawDetour(void* self, void*, const float amount) {
    float corrected = amount;
    if (g_mouseAxisCorrectionArmed) {
        const double scaled = static_cast<double>(amount) *
                              static_cast<double>(g_mouseYawScale);
        if (std::isfinite(scaled) &&
            scaled >= -std::numeric_limits<float>::max() &&
            scaled <= std::numeric_limits<float>::max()) {
            corrected = static_cast<float>(scaled);
            ++g_mouseStats.yawCorrections;
            g_mouseStats.yawScaleSum += g_mouseYawScale;
        } else {
            ++g_mouseStats.yawScaleUnavailable;
        }
    }
    g_playerAddYaw(self, corrected);
}

void __fastcall SceneUpdateRenderListDetour(void* self, void*,
                                            const float frameTime) {
    g_counters.renderLists.fetch_add(1, std::memory_order_relaxed);
    Record(EventKind::RenderList);
    if (g_worldRootInterpolationEnabled || g_handsRootInterpolationEnabled) {
        g_meshCache.BeginCollection();
    }
    if (PoseInterpolationEnabled()) {
        g_poseRuntime.BeginCollection(g_tickSerial,
                                      g_presentationClock.Generation());
    }
    if (g_particleInterpolationEnabled) {
        g_particleRuntime.BeginCollection();
    }
    const std::int64_t renderListBegin = OverheadNow();
    g_sceneUpdateRenderList(self, frameTime);
    const std::int64_t renderListEnd = OverheadNow();
    if (renderListBegin != 0 && renderListEnd != 0) {
        g_overheadStats.renderList.Add(renderListEnd - renderListBegin);
        ++g_overheadStats.renderLists;
    }
    if (g_particleInterpolationEnabled) {
        g_particleRuntime.EndCollection();
    }
    if (PoseInterpolationEnabled()) {
        g_poseRuntime.EndCollection();
    }
    if (g_worldRootInterpolationEnabled || g_handsRootInterpolationEnabled) {
        g_meshCache.EndCollection();
    }
}

void __fastcall SceneRenderDetour(void* self, void*, void* updater,
                                  const float frameTime) {
    const bool refreshMeshCaches =
        g_worldRootInterpolationEnabled || g_handsRootInterpolationEnabled;
    const bool poseEnabled = PoseInterpolationEnabled();
    const bool entityRootEnabled = g_worldPoseInterpolationEnabled;
    g_counters.sceneRenders.fetch_add(1, std::memory_order_relaxed);
    g_renderFrameTimeSum += frameTime;
    if (g_renderFrameTimeSamples == 0) {
        g_renderFrameTimeMin = frameTime;
        g_renderFrameTimeMax = frameTime;
    } else {
        g_renderFrameTimeMin = (std::min)(g_renderFrameTimeMin, frameTime);
        g_renderFrameTimeMax = (std::max)(g_renderFrameTimeMax, frameTime);
    }
    ++g_renderFrameTimeSamples;

    bool timerPhaseValid = false;
    clock::Sample phase{};
    if (g_drawPhaseReady) {
        phase = g_drawPhase;
        timerPhaseValid = g_drawPhaseTimerValid;
        g_drawPhaseReady = false;
    } else {
        phase = EvaluatePresentationPhase(timerPhaseValid);
    }
    if (!timerPhaseValid) {
        ++g_phaseStats.invalidTimer;
    }
    if (phase.kind == clock::SampleKind::interpolated) {
        if (g_phaseStats.interpolated == 0) {
            g_phaseStats.alphaMin = phase.alpha;
            g_phaseStats.alphaMax = phase.alpha;
        } else {
            g_phaseStats.alphaMin = (std::min)(g_phaseStats.alphaMin, phase.alpha);
            g_phaseStats.alphaMax = (std::max)(g_phaseStats.alphaMax, phase.alpha);
        }
        ++g_phaseStats.interpolated;
        g_phaseStats.alphaSum += phase.alpha;
        if (g_hasLastPhase &&
            phase.generation == g_lastPhaseGeneration &&
            phase.newerSerial == g_lastPhaseNewerSerial &&
            phase.alpha + 1.0e-6 < g_lastAlpha) {
            ++g_phaseStats.monotonicViolations;
        }
        g_hasLastPhase = true;
        g_lastPhaseGeneration = phase.generation;
        g_lastPhaseNewerSerial = phase.newerSerial;
        g_lastAlpha = phase.alpha;
    } else if (phase.kind == clock::SampleKind::reset) {
        ++g_phaseStats.resets;
        g_phaseStats.lastResetReason = phase.resetReason;
        g_hasLastPhase = false;
    } else {
        ++g_phaseStats.unavailable;
    }
    const std::int64_t prepareBegin = OverheadNow();
    std::int64_t worldTicks = 0;
    std::int64_t particleTicks = 0;
    std::int64_t poseTicks = 0;
    std::int64_t meshCacheTicks = 0;
    bool cameraApplied = false;
    transform::Matrix mouseHandsCorrection = transform::Identity();
    bool mouseHandsCorrectionReady = false;
    if (g_cameraInterpolationEnabled) {
        const camera::FrameResult result =
            g_cameraRuntime.Apply(self, g_tickSerial, phase);
        ++g_cameraResults[static_cast<std::size_t>(result)];
        cameraApplied = result == camera::FrameResult::applied;
    }
    if (g_mouseOverlayEnabled && cameraApplied) {
        PollPresentationMouse();
        mouseHandsCorrectionReady =
            ApplyMousePreview(phase, mouseHandsCorrection);
    }
    if (g_worldRootInterpolationEnabled) {
        const std::int64_t begin = OverheadNow();
        AddWorldBatch(g_worldRuntime.BeginRender(phase));
        worldTicks += OverheadNow() - begin;
    }
    if (entityRootEnabled) {
        AddEntityRootBatch(g_entityRootRuntime.BeginRender(phase));
    }
    if (g_handsRootInterpolationEnabled) {
        if (!g_handsRuntime.SetPresentationCorrection(
                mouseHandsCorrectionReady ? &mouseHandsCorrection : nullptr)) {
            ++g_mouseStats.handsCorrectionFailed;
            mouseHandsCorrectionReady = false;
        }
        const hands::BatchResult handsBatch =
            g_handsRuntime.BeginRender(phase);
        AddHandsBatch(handsBatch);
        if (mouseHandsCorrectionReady && handsBatch.ready != 0) {
            ++g_mouseStats.handsCorrectionApplied;
        }
    }
    if (refreshMeshCaches) {
        const std::int64_t begin = OverheadNow();
        AddMeshRefresh(g_meshCache.Refresh());
        meshCacheTicks += OverheadNow() - begin;
    }
    if (g_particleInterpolationEnabled) {
        const std::int64_t begin = OverheadNow();
        const particle::BatchResult particles =
            g_particleRuntime.Present(phase);
        particleTicks += OverheadNow() - begin;
        AddParticleBatch(particles);
        if (particles.restoreFailed != 0) {
            g_particleInterpolationEnabled = false;
            logger::Write(
                L"ERROR: particle position restoration failed; particle "
                L"interpolation has been disabled for the rest of this process.");
        }
    }
    if (poseEnabled) {
        const std::int64_t begin = OverheadNow();
        AddPoseBatch(g_poseRuntime.BeginRender(phase));
        poseTicks += OverheadNow() - begin;
    }
    Record(EventKind::SceneRender);
    const std::int64_t prepareEnd = OverheadNow();
    g_sceneRender(self, updater, frameTime);
    const std::int64_t finishBegin = OverheadNow();
    if (poseEnabled) {
        const pose::BatchResult ended = g_poseRuntime.EndRender();
        poseTicks += OverheadNow() - finishBegin;
        AddPoseBatch(ended);
        if (ended.restoreFailed != 0) {
            g_handsPoseInterpolationEnabled = false;
            g_worldPoseInterpolationEnabled = false;
            g_ragdollPoseInterpolationEnabled = false;
            logger::Write(
                L"ERROR: skeletal pose restoration failed; all pose "
                L"interpolation has been disabled for the rest of this process.");
        }
    }
    if (g_handsRootInterpolationEnabled) {
        const hands::BatchResult ended = g_handsRuntime.EndRender();
        AddHandsBatch(ended);
        if (ended.restoreFailed != 0) {
            g_handsRootInterpolationEnabled = false;
            logger::Write(
                L"ERROR: hands-root restoration failed; hands-root "
                L"interpolation has been disabled for the rest of this process.");
        }
    }
    if (entityRootEnabled) {
        const entity_root::BatchResult ended =
            g_entityRootRuntime.EndRender();
        AddEntityRootBatch(ended);
        if (ended.restoreFailed != 0) {
            g_worldPoseInterpolationEnabled = false;
            logger::Write(
                L"ERROR: gameplay mesh-root restoration failed; world-pose "
                L"interpolation has been disabled for the rest of this process.");
        }
    }
    if (g_worldRootInterpolationEnabled) {
        const std::int64_t begin = OverheadNow();
        const world::BatchResult ended = g_worldRuntime.EndRender();
        worldTicks += OverheadNow() - begin;
        AddWorldBatch(ended);
        if (ended.restoreFailed != 0) {
            g_worldRootInterpolationEnabled = false;
            logger::Write(
                L"ERROR: world-root restoration failed; world-root "
                L"interpolation has been disabled for the rest of this process.");
        }
    }
    if (refreshMeshCaches) {
        const std::int64_t begin = OverheadNow();
        AddMeshRefresh(g_meshCache.Refresh());
        meshCacheTicks += OverheadNow() - begin;
    }
    if (cameraApplied) {
        const camera::FrameResult restored = g_cameraRuntime.Restore();
        ++g_cameraResults[static_cast<std::size_t>(restored)];
        if (restored == camera::FrameResult::restoreFailed) {
            g_cameraInterpolationEnabled = false;
            logger::Write(
                L"ERROR: camera restoration failed; camera interpolation "
                L"has been disabled for the rest of this process.");
        }
    }
    const std::int64_t finishEnd = OverheadNow();
    if (prepareBegin != 0 && finishEnd != 0) {
        const std::int64_t prepare = prepareEnd - prepareBegin;
        const std::int64_t finish = finishEnd - finishBegin;
        g_overheadStats.prepare.Add(prepare);
        g_overheadStats.finish.Add(finish);
        g_overheadStats.total.Add(prepare + finish);
        g_overheadStats.world.Add(worldTicks);
        g_overheadStats.particles.Add(particleTicks);
        g_overheadStats.pose.Add(poseTicks);
        g_overheadStats.meshCache.Add(meshCacheTicks);
        g_overheadStats.sceneRender.Add(finishBegin - prepareEnd);
        ++g_overheadStats.frames;
    }
}

void __cdecl SdlGlSwapBuffersDetour() {
    g_counters.swaps.fetch_add(1, std::memory_order_relaxed);
    Record(EventKind::Swap);
    g_sdlGlSwapBuffers();
    LARGE_INTEGER counter{};
    if (g_qpcFrequency > 0 && QueryPerformanceCounter(&counter)) {
        g_swapCadence.RecordQpc(counter.QuadPart, g_qpcFrequency);
    }
    if (g_telemetryEnabled) {
        MaybeReport();
    }
}

void __cdecl NewtonUpdateDetour(void* world, const float step) {
    g_counters.physicsSteps.fetch_add(1, std::memory_order_relaxed);
    g_physicsStepSum += step;
    ++g_physicsStepSamples;
    Record(EventKind::PhysicsStep);
    g_insideNewtonUpdate = true;
    g_newtonUpdate(world, step);
    g_insideNewtonUpdate = false;
}

void __fastcall NodeSetMatrixDetour(void* self, void*,
                                    const transform::Matrix& matrix,
                                    const bool updateChildren) {
    g_nodeSetMatrix(self, matrix, updateChildren);
    if (!g_worldRootInterpolationEnabled || !g_insideNewtonUpdate) {
        return;
    }
    const world::NotifyResult result = g_worldRuntime.NotifyNodeMoved(
        self, matrix, g_tickSerial + 1U);
    switch (result) {
    case world::NotifyResult::staged:
        ++g_worldStaged;
        break;
    case world::NotifyResult::invalid:
        ++g_worldNotifyInvalid;
        break;
    case world::NotifyResult::capacityExhausted:
        ++g_worldCapacity;
        break;
    }
}

void __fastcall PlayerHandsUpdateDetour(void* self, void*, const float step) {
    g_insidePlayerHandsUpdate = true;
    g_currentPlayerHands = self;
    g_playerHandsUpdate(self, step);
    g_currentPlayerHands = nullptr;
    g_insidePlayerHandsUpdate = false;
}

void __fastcall EntitySetMatrixDetour(void* self, void*,
                                      const transform::Matrix& matrix) {
    g_entitySetMatrix(self, matrix);
    if (g_handsPoseInterpolationEnabled && g_insidePlayerHandsUpdate) {
        g_poseRuntime.MarkHandsMesh(self, g_tickSerial + 1U);
    }
    if (g_worldPoseInterpolationEnabled && g_insideUpdaterUpdate &&
        !g_insidePlayerHandsUpdate) {
        const entity_root::NotifyResult result =
            g_entityRootRuntime.NotifyMoved(self, matrix,
                                            g_tickSerial + 1U);
        switch (result) {
        case entity_root::NotifyResult::staged:
            ++g_entityRootStaged;
            break;
        case entity_root::NotifyResult::ignored:
            ++g_entityRootIgnored;
            break;
        case entity_root::NotifyResult::invalid:
            ++g_entityRootNotifyInvalid;
            break;
        case entity_root::NotifyResult::capacityExhausted:
            ++g_entityRootCapacity;
            break;
        }
    }
    if (!g_handsRootInterpolationEnabled || !g_insidePlayerHandsUpdate) {
        return;
    }
    const hands::NotifyResult result = g_handsRuntime.NotifyRootMoved(
        g_currentPlayerHands, self, matrix, g_tickSerial + 1U);
    switch (result) {
    case hands::NotifyResult::staged:
        ++g_handsStaged;
        break;
    case hands::NotifyResult::ignored:
        ++g_handsIgnored;
        break;
    case hands::NotifyResult::invalid:
        ++g_handsNotifyInvalid;
        break;
    case hands::NotifyResult::capacityExhausted:
        ++g_handsCapacity;
        break;
    }
}

transform::Matrix* __fastcall MeshGetModelMatrixDetour(void* self, void*,
                                                       void* camera) {
    transform::Matrix* const result = g_meshGetModelMatrix(self, camera);
    if (!g_worldRootInterpolationEnabled && !g_handsRootInterpolationEnabled) {
        return result;
    }
    switch (g_meshCache.Observe(self)) {
    case mesh::ObserveResult::added:
        ++g_meshCollected;
        break;
    case mesh::ObserveResult::duplicate:
        ++g_meshDuplicates;
        break;
    case mesh::ObserveResult::capacityExhausted:
        ++g_meshCapacity;
        break;
    case mesh::ObserveResult::inactive:
        break;
    }
    return result;
}

void __fastcall MeshUpdateGraphicsDetour(void* self, void*, void* camera,
                                         const float frameTime,
                                         void* renderList) {
    g_meshUpdateGraphics(self, camera, frameTime, renderList);
    switch (g_poseRuntime.ObserveMesh(self)) {
    case pose::ObserveMeshResult::captured:
        ++g_poseCaptured;
        break;
    case pose::ObserveMeshResult::unchanged:
        ++g_poseUnchanged;
        break;
    case pose::ObserveMeshResult::ignored:
        ++g_poseIgnored;
        break;
    case pose::ObserveMeshResult::historyReset:
        ++g_poseHistoryReset;
        break;
    case pose::ObserveMeshResult::invalid:
        ++g_poseInvalid;
        break;
    case pose::ObserveMeshResult::capacityExhausted:
        ++g_poseCapacity;
        break;
    }
}

void __fastcall SubMeshUpdateGraphicsDetour(void* self, void*, void* camera,
                                            const float frameTime,
                                            void* renderList) {
    g_subMeshUpdateGraphics(self, camera, frameTime, renderList);
    switch (g_poseRuntime.ObserveSubMesh(self, camera, frameTime,
                                        renderList)) {
    case pose::ObserveSubMeshResult::added:
        ++g_poseSubMeshes;
        break;
    case pose::ObserveSubMeshResult::duplicate:
        ++g_poseSubMeshDuplicates;
        break;
    case pose::ObserveSubMeshResult::ignored:
        break;
    case pose::ObserveSubMeshResult::invalid:
        ++g_poseSubMeshInvalid;
        break;
    case pose::ObserveSubMeshResult::capacityExhausted:
        ++g_poseSubMeshCapacity;
        break;
    }
}

void __fastcall ParticleUpdateGraphicsDetour(void* self, void*, void* camera,
                                             const float frameTime,
                                             void* renderList) {
    g_particleUpdateGraphics(self, camera, frameTime, renderList);
    if (!g_particleInterpolationEnabled) {
        return;
    }
    switch (g_particleRuntime.Observe(self, camera, frameTime, renderList)) {
    case particle::ObserveResult::added:
        ++g_particleCollected;
        break;
    case particle::ObserveResult::duplicate:
        ++g_particleDuplicates;
        break;
    case particle::ObserveResult::addedLine:
        ++g_particleCollected;
        ++g_particleLines;
        break;
    case particle::ObserveResult::ignoredBeam:
        ++g_particleBeamsSkipped;
        break;
    case particle::ObserveResult::invalid:
        ++g_particleInvalid;
        break;
    case particle::ObserveResult::capacityExhausted:
        ++g_particleCapacity;
        break;
    }
}

void __fastcall ImageTrailRenderDetour(void* self, void*) {
    if (!g_imageTrailTimingEnabled) {
        g_imageTrailRender(self);
        return;
    }

    ++g_imageTrailStats.calls;
    const double now = WallTimeSeconds();
    const float elapsed = g_lastImageTrailWallTime > 0.0 &&
                                  now > g_lastImageTrailWallTime
                              ? static_cast<float>(
                                    now - g_lastImageTrailWallTime)
                              : 0.0F;
    g_lastImageTrailWallTime = now;

    const image_trail::BeginResult begin =
        g_imageTrailRuntime.Begin(self, elapsed);
    switch (begin.kind) {
    case image_trail::BeginKind::inactive:
        ++g_imageTrailStats.inactive;
        break;
    case image_trail::BeginKind::firstPass:
        ++g_imageTrailStats.firstPass;
        break;
    case image_trail::BeginKind::applied:
        ++g_imageTrailStats.applied;
        g_imageTrailStats.frameTimeSum += elapsed;
        g_imageTrailStats.stockAmountSum += begin.stockAmount;
        g_imageTrailStats.correctedAmountSum += begin.correctedAmount;
        break;
    case image_trail::BeginKind::invalid:
        ++g_imageTrailStats.invalid;
        break;
    case image_trail::BeginKind::reentrant:
        ++g_imageTrailStats.reentrant;
        break;
    }

    g_imageTrailRender(self);
    if (begin.kind != image_trail::BeginKind::applied) {
        return;
    }
    const image_trail::EndKind ended = g_imageTrailRuntime.End();
    if (ended == image_trail::EndKind::restored) {
        ++g_imageTrailStats.restored;
        return;
    }
    ++g_imageTrailStats.restoreFailed;
    g_imageTrailTimingEnabled = false;
    logger::Write(
        L"ERROR: image-trail amount restoration failed; image-trail timing "
        L"has been disabled for the rest of this process.");
}

void __fastcall MotionBlurRenderDetour(void* self, void*) {
    if (!g_motionBlurTimingEnabled) {
        g_motionBlurRender(self);
        return;
    }

    ++g_motionBlurStats.calls;
    const double now = WallTimeSeconds();
    const float elapsed = g_lastMotionBlurWallTime > 0.0 &&
                                  now > g_lastMotionBlurWallTime
                              ? static_cast<float>(
                                    now - g_lastMotionBlurWallTime)
                              : 0.0F;
    g_lastMotionBlurWallTime = now;

    const motion_blur::BeginResult begin =
        g_motionBlurRuntime.Begin(self, elapsed);
    switch (begin.kind) {
    case motion_blur::BeginKind::inactive:
        ++g_motionBlurStats.inactive;
        break;
    case motion_blur::BeginKind::unavailable:
        ++g_motionBlurStats.unavailable;
        break;
    case motion_blur::BeginKind::firstPass:
        ++g_motionBlurStats.firstPass;
        break;
    case motion_blur::BeginKind::applied:
        ++g_motionBlurStats.applied;
        g_motionBlurStats.frameTimeSum += elapsed;
        g_motionBlurStats.stockScaleSum += begin.stockScale;
        g_motionBlurStats.correctedScaleSum += begin.correctedScale;
        break;
    case motion_blur::BeginKind::invalid:
        ++g_motionBlurStats.invalid;
        break;
    case motion_blur::BeginKind::reentrant:
        ++g_motionBlurStats.reentrant;
        break;
    }

    g_motionBlurRender(self);
    if (begin.kind != motion_blur::BeginKind::applied) {
        return;
    }
    const motion_blur::EndKind ended = g_motionBlurRuntime.End();
    if (ended == motion_blur::EndKind::restored) {
        ++g_motionBlurStats.restored;
        return;
    }
    ++g_motionBlurStats.restoreFailed;
    g_motionBlurTimingEnabled = false;
    logger::Write(
        L"ERROR: motion-blur scale restoration failed; motion-blur timing "
        L"has been disabled for the rest of this process.");
}

int __cdecl SdlPollEventDetour(void* event) {
    const int result = g_sdlPollEvent(event);
    g_counters.pollEventCalls.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) {
        g_counters.pollEventsReturned.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

std::uint8_t __cdecl SdlGetRelativeMouseStateDetour(int* x, int* y) {
    g_counters.relativeMouseCalls.fetch_add(1, std::memory_order_relaxed);
    int freshX = 0;
    int freshY = 0;
    const std::uint8_t buttons =
        g_sdlGetRelativeMouseState(&freshX, &freshY);
    if ((!g_mouseOverlayEnabled && !g_guiMouseOverlayEnabled) ||
        !g_insideUpdaterUpdate ||
        !g_insideMouseSdlUpdate) {
        if (x != nullptr) {
            *x = freshX;
        }
        if (y != nullptr) {
            *y = freshY;
        }
        return buttons;
    }

    MouseContext context{};
    if (ReadEligibleMouseContext(context) == MouseEligibility::eligible) {
        int deliveredX = 0;
        int deliveredY = 0;
        if (!mouse::CombineForDelivery(g_pendingMouseCounts, 0, 0,
                                       deliveredX, deliveredY)) {
            ++g_mouseStats.overflows;
            deliveredX = freshX;
            deliveredY = freshY;
        } else {
            g_deliveredMouseCounts = g_pendingMouseCounts;
            g_pendingMouseCounts = {};
            if (!mouse::Accumulate(g_pendingMouseCounts, freshX, freshY)) {
                ++g_mouseStats.overflows;
                g_pendingMouseCounts = {};
                int combinedX = freshX;
                int combinedY = freshY;
                if (mouse::CombineForDelivery(g_deliveredMouseCounts, freshX,
                                              freshY, combinedX, combinedY)) {
                    deliveredX = combinedX;
                    deliveredY = combinedY;
                    g_deliveredMouseCounts = {};
                }
            } else {
                g_mouseStats.deferredFreshCounts += CountMagnitude({freshX, freshY});
            }
        }
        const std::uint64_t delivered =
            CountMagnitude(g_deliveredMouseCounts);
        if (delivered != 0) {
            ++g_mouseStats.fixedDeliveries;
            g_mouseStats.deliveredCounts += delivered;
            g_deliveryAwaitingButtonHandler = true;
        }
        if (x != nullptr) {
            *x = deliveredX;
        }
        if (y != nullptr) {
            *y = deliveredY;
        }
        return buttons;
    }

    GuiMouseContext guiContext{};
    if (ReadEligibleGuiMouseContext(guiContext)) {
        int deliveredX = freshX;
        int deliveredY = freshY;
        if (mouse::CombineForDelivery(g_pendingMouseCounts, freshX, freshY,
                                      deliveredX, deliveredY)) {
            if (CountMagnitude(g_pendingMouseCounts) != 0) {
                ++g_mouseStats.immediateFallbacks;
            }
            g_pendingMouseCounts = {};
        } else {
            ++g_mouseStats.overflows;
        }
        const std::uint64_t delivered =
            CountMagnitude({deliveredX, deliveredY});
        if (delivered != 0) {
            ++g_mouseStats.fixedDeliveries;
            g_mouseStats.deliveredCounts += delivered;
        }
        g_deliveryAwaitingButtonHandler = false;
        g_deliveredMouseCounts = {};
        g_deliveryCandidateReady = false;
        g_deliveryBridgeValid = false;
        if (x != nullptr) {
            *x = deliveredX;
        }
        if (y != nullptr) {
            *y = deliveredY;
        }
        return buttons;
    }

    int deliveredX = freshX;
    int deliveredY = freshY;
    if (mouse::CombineForDelivery(g_pendingMouseCounts, freshX, freshY,
                                  deliveredX, deliveredY)) {
        if (CountMagnitude(g_pendingMouseCounts) != 0) {
            ++g_mouseStats.immediateFallbacks;
        }
        g_pendingMouseCounts = {};
    } else {
        ++g_mouseStats.overflows;
    }
    g_deliveryAwaitingButtonHandler = false;
    g_deliveredMouseCounts = {};
    g_deliveryCandidateReady = false;
    g_deliveryBridgeValid = false;
    if (x != nullptr) {
        *x = deliveredX;
    }
    if (y != nullptr) {
        *y = deliveredY;
    }
    return buttons;
}

std::wstring HookError(const wchar_t* operation, const MH_STATUS status) {
    const char* text = MH_StatusToString(status);
    std::wstring wide;
    if (text != nullptr) {
        while (*text != '\0') {
            wide.push_back(static_cast<unsigned char>(*text));
            ++text;
        }
    }
    return std::wstring(operation) + L" failed: " + wide;
}

void RollBack() noexcept {
    g_interactionCursorRuntime.Reset();
    g_guiCursorRuntime.Reset();
    g_particleRuntime.Reset();
    g_imageTrailRuntime.Reset();
    g_motionBlurRuntime.Reset();
    g_pendingMouseCounts = {};
    g_deliveryBridgeValid = false;
    g_drawPhaseReady = false;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_installed = false;
}

template <typename Target, typename Detour>
bool CreateHook(void* target, Detour detour, Target& original,
                const wchar_t* name, std::wstring& error) {
    const MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void*>(detour),
        reinterpret_cast<void**>(&original));
    if (status != MH_OK) {
        error = HookError(name, status);
        return false;
    }
    return true;
}

} // namespace

bool Install(const target::Mapping& mapping, const int reportIntervalSeconds,
             const bool telemetryEnabled, const bool renderUnlockEnabled,
             const bool cameraInterpolationEnabled,
             const bool handsRootInterpolationEnabled,
             const bool worldRootInterpolationEnabled,
             const bool handsPoseInterpolationEnabled,
             const bool worldPoseInterpolationEnabled,
             const bool ragdollPoseInterpolationEnabled,
             const bool particleInterpolationEnabled,
             const bool imageTrailTimingEnabled,
             const bool motionBlurTimingEnabled,
             const bool mouseOverlayEnabled,
             const bool mouseAxisCorrectionEnabled,
             const bool interactionCursorInterpolationEnabled,
             const bool guiMouseOverlayEnabled,
             std::wstring& error) noexcept {
    if (g_installed) {
        error = L"Telemetry hooks are already installed";
        return false;
    }
    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK) {
        error = HookError(L"MH_Initialize", initializeStatus);
        return false;
    }

    g_telemetryEnabled = telemetryEnabled;
    g_renderUnlockEnabled = renderUnlockEnabled;
    g_cameraInterpolationEnabled = cameraInterpolationEnabled;
    g_handsRootInterpolationEnabled = handsRootInterpolationEnabled;
    g_worldRootInterpolationEnabled = worldRootInterpolationEnabled;
    g_handsPoseInterpolationEnabled = handsPoseInterpolationEnabled;
    g_worldPoseInterpolationEnabled = worldPoseInterpolationEnabled;
    g_ragdollPoseInterpolationEnabled = ragdollPoseInterpolationEnabled;
    g_particleInterpolationEnabled = particleInterpolationEnabled;
    g_imageTrailTimingEnabled = imageTrailTimingEnabled;
    g_motionBlurTimingEnabled = motionBlurTimingEnabled;
    g_mouseOverlayEnabled =
        mouseOverlayEnabled && cameraInterpolationEnabled;
    g_mouseAxisCorrectionEnabled = mouseAxisCorrectionEnabled;
    g_interactionCursorInterpolationEnabled =
        interactionCursorInterpolationEnabled;
    g_guiMouseOverlayEnabled = guiMouseOverlayEnabled;
    g_cameraRuntime.Configure({mapping.cameraSetPosition, mapping.cameraSetPitch,
                               mapping.cameraSetYaw, mapping.cameraSetRoll});
    bool created = true;
    if (renderUnlockEnabled) {
        created = CreateHook(mapping.gameRun, &GameRunDetour, g_gameRun,
                             L"cGame::Run", error);
    }
    const bool timingNeeded = telemetryEnabled || cameraInterpolationEnabled ||
                              handsRootInterpolationEnabled ||
                              worldRootInterpolationEnabled ||
                              PoseInterpolationEnabled() ||
                              particleInterpolationEnabled ||
                              g_mouseOverlayEnabled ||
                              g_interactionCursorInterpolationEnabled ||
                              g_guiMouseOverlayEnabled;
    if (created && timingNeeded) {
        created =
            CreateHook(mapping.logicTimerReset, &LogicTimerResetDetour,
                       g_logicTimerReset, L"cLogicTimer::Reset", error) &&
            CreateHook(mapping.updaterUpdate, &UpdaterUpdateDetour,
                       g_updaterUpdate, L"cUpdater::Update", error) &&
            CreateHook(mapping.sceneRender, &SceneRenderDetour, g_sceneRender,
                       L"cScene::Render", error);
    }
    if (created &&
        (telemetryEnabled || g_interactionCursorInterpolationEnabled ||
         g_guiMouseOverlayEnabled)) {
        created = CreateHook(mapping.updaterOnDraw, &UpdaterOnDrawDetour,
                             g_updaterOnDraw, L"cUpdater::OnDraw", error);
    }
    if (created && telemetryEnabled) {
        created =
            CreateHook(mapping.updaterOnPostBufferSwap,
                       &UpdaterOnPostBufferSwapDetour,
                       g_updaterOnPostBufferSwap,
                       L"cUpdater::OnPostBufferSwap", error) &&
            CreateHook(mapping.sdlGlSwapBuffers, &SdlGlSwapBuffersDetour,
                       g_sdlGlSwapBuffers, L"SDL_GL_SwapBuffers", error) &&
            CreateHook(mapping.sdlPollEvent, &SdlPollEventDetour,
                       g_sdlPollEvent, L"SDL_PollEvent", error);
    }
    if (created && (telemetryEnabled || g_mouseOverlayEnabled ||
                    g_guiMouseOverlayEnabled)) {
        created = CreateHook(mapping.sdlGetRelativeMouseState,
                             &SdlGetRelativeMouseStateDetour,
                             g_sdlGetRelativeMouseState,
                             L"SDL_GetRelativeMouseState", error);
    }
    if (created &&
        (g_mouseOverlayEnabled || g_mouseAxisCorrectionEnabled ||
         g_interactionCursorInterpolationEnabled ||
         g_guiMouseOverlayEnabled)) {
        created =
            CreateHook(mapping.mouseSdlUpdate, &MouseSdlUpdateDetour,
                       g_mouseSdlUpdate, L"cMouseSDL::Update", error) &&
            CreateHook(mapping.buttonHandlerUpdate,
                       &ButtonHandlerUpdateDetour, g_buttonHandlerUpdate,
                       L"cButtonHandler::Update", error);
    }
    if (created && g_mouseAxisCorrectionEnabled) {
        created = CreateHook(mapping.playerAddYaw, &PlayerAddYawDetour,
                             g_playerAddYaw, L"cPlayer::AddYaw", error);
    }
    if (created && (telemetryEnabled || handsRootInterpolationEnabled ||
                    worldRootInterpolationEnabled ||
                    PoseInterpolationEnabled() ||
                    particleInterpolationEnabled)) {
        created = CreateHook(mapping.sceneUpdateRenderList,
                             &SceneUpdateRenderListDetour,
                             g_sceneUpdateRenderList,
                             L"cScene::UpdateRenderList", error);
    }
    if (created && (telemetryEnabled || worldRootInterpolationEnabled)) {
        created = CreateHook(mapping.newtonUpdate, &NewtonUpdateDetour,
                             g_newtonUpdate, L"NewtonUpdate", error);
    }
    if (created && worldRootInterpolationEnabled) {
        created = CreateHook(mapping.nodeSetMatrix, &NodeSetMatrixDetour,
                             g_nodeSetMatrix, L"cNode3D::SetMatrix", error);
    }
    if (created &&
        (handsRootInterpolationEnabled || handsPoseInterpolationEnabled ||
         worldPoseInterpolationEnabled)) {
        created =
            CreateHook(mapping.playerHandsUpdate, &PlayerHandsUpdateDetour,
                       g_playerHandsUpdate, L"cPlayerHands::Update", error) &&
            CreateHook(mapping.entitySetMatrix, &EntitySetMatrixDetour,
                       g_entitySetMatrix, L"iEntity3D::SetMatrix", error);
    }
    if (created &&
        (worldRootInterpolationEnabled || handsRootInterpolationEnabled)) {
        created = CreateHook(mapping.meshGetModelMatrix,
                             &MeshGetModelMatrixDetour,
                             g_meshGetModelMatrix,
                             L"cMeshEntity::GetModelMatrix", error);
    }
    if (created && PoseInterpolationEnabled()) {
        created =
            CreateHook(mapping.meshUpdateGraphics,
                       &MeshUpdateGraphicsDetour, g_meshUpdateGraphics,
                       L"cMeshEntity::UpdateGraphics", error) &&
            CreateHook(mapping.subMeshUpdateGraphics,
                       &SubMeshUpdateGraphicsDetour,
                       g_subMeshUpdateGraphics,
                       L"cSubMeshEntity::UpdateGraphics", error);
    }
    if (created && particleInterpolationEnabled) {
        created = CreateHook(
            mapping.particleEmitterUpdateGraphics,
            &ParticleUpdateGraphicsDetour, g_particleUpdateGraphics,
            L"iParticleEmitter3D::UpdateGraphics", error);
    }
    if (created && imageTrailTimingEnabled) {
        created = CreateHook(
            mapping.rendererPostEffectsRenderImageTrail,
            &ImageTrailRenderDetour, g_imageTrailRender,
            L"cRendererPostEffects::RenderImageTrail", error);
    }
    if (created && motionBlurTimingEnabled) {
        created = CreateHook(
            mapping.rendererPostEffectsRenderMotionBlur,
            &MotionBlurRenderDetour, g_motionBlurRender,
            L"cRendererPostEffects::RenderMotionBlur", error);
    }
    if (!created) {
        RollBack();
        return false;
    }
    if (worldRootInterpolationEnabled) {
        g_worldRuntime.Configure(reinterpret_cast<void*>(g_nodeSetMatrix));
    }
    if (handsRootInterpolationEnabled) {
        g_handsRuntime.Configure(reinterpret_cast<void*>(g_entitySetMatrix),
                                 mapping.meshGetModelMatrix);
    }
    if (worldPoseInterpolationEnabled) {
        g_entityRootRuntime.Configure(
            reinterpret_cast<void*>(g_entitySetMatrix),
            mapping.meshGetModelMatrix);
    }
    if (worldRootInterpolationEnabled || handsRootInterpolationEnabled) {
        g_meshCache.Configure(
            reinterpret_cast<void*>(g_meshGetModelMatrix),
            mapping.meshGetModelMatrix,
            mapping.renderableGetInvModelMatrix);
    }
    if (PoseInterpolationEnabled()) {
        g_poseRuntime.Configure(
            {reinterpret_cast<void*>(g_subMeshUpdateGraphics),
             mapping.meshUpdateGraphics,
             mapping.subMeshUpdateGraphics},
            {handsPoseInterpolationEnabled, worldPoseInterpolationEnabled,
             ragdollPoseInterpolationEnabled});
    }
    if (particleInterpolationEnabled) {
        g_particleRuntime.Configure(
            {reinterpret_cast<void*>(g_particleUpdateGraphics),
             mapping.particleEmitterUpdateGraphics});
    }

    g_reportIntervalMs = static_cast<DWORD>(reportIntervalSeconds) * 1000U;
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency)) {
        g_qpcFrequency = frequency.QuadPart;
    }
    g_sdlGetTicks = reinterpret_cast<SdlGetTicksFn>(mapping.sdlGetTicks);
    g_sdlPumpEvents = reinterpret_cast<SdlPumpEventsFn>(mapping.sdlPumpEvents);
    g_lastReportMs.store(GetTickCount64(), std::memory_order_relaxed);
    const MH_STATUS enableStatus = MH_QueueEnableHook(MH_ALL_HOOKS);
    if (enableStatus != MH_OK) {
        error = HookError(L"MH_QueueEnableHook", enableStatus);
        RollBack();
        return false;
    }
    const MH_STATUS applyStatus = MH_ApplyQueued();
    if (applyStatus != MH_OK) {
        error = HookError(L"MH_ApplyQueued", applyStatus);
        RollBack();
        return false;
    }
    g_installed = true;
    return true;
}

void Shutdown() noexcept {
    if (g_installed) {
        RollBack();
    }
}

} // namespace pbpfps::telemetry
