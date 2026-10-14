#include "bootstrap.h"

#include "config.h"
#include "host_validation.h"
#include "logger.h"
#include "target_host.h"
#include "target_layout.h"
#include "target_mapping.h"
#include "telemetry.h"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>

namespace pbpfps {
namespace {

std::wstring ModulePath(const HMODULE module) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(module, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

std::wstring IniPath(const HMODULE module) {
    std::wstring path = ModulePath(module);
    const std::size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        path.resize(dot);
    }
    path += L".ini";
    return path;
}

bool WaitForRuntimeModules() noexcept {
    constexpr DWORD kTimeoutMilliseconds = 10000;
    constexpr DWORD kPollMilliseconds = 25;
    DWORD elapsed = 0;
    while (elapsed < kTimeoutMilliseconds) {
        if (GetModuleHandleW(L"SDL.dll") != nullptr &&
            GetModuleHandleW(L"Newton.dll") != nullptr) {
            return true;
        }
        Sleep(kPollMilliseconds);
        elapsed += kPollMilliseconds;
    }
    return false;
}

std::wstring BoolText(const bool value) { return value ? L"true" : L"false"; }

std::wstring PointerText(const void* pointer) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase
           << reinterpret_cast<std::uintptr_t>(pointer);
    return stream.str();
}

} // namespace

DWORD WINAPI BootstrapThread(void* const parameter) noexcept {
    const HMODULE plugin = static_cast<HMODULE>(parameter);
    if (!logger::Initialize(plugin)) {
        return 1;
    }

    logger::Write(std::wstring(L"PenumbraBlackPlague.FPSFix ") +
                  PENUMBRA_BLACK_PLAGUE_FPS_FIX_VERSION +
                  L" diagnostic bootstrap starting (x86)");
    logger::Write(L"Log path: " + logger::Path());

    const std::wstring iniPath = IniPath(plugin);
    Settings settings = LoadSettings(iniPath);
    logger::Write(L"Configuration: " + iniPath);
    if (!settings.enabled) {
        logger::Write(L"Plugin disabled by configuration; no host scan performed.");
        return 0;
    }

    const host::Report report = host::ValidateMainImage();
    if (!report.compatibleForDiagnostics) {
        logger::Write(L"Host validation failed closed: " + report.error);
        return 2;
    }

    if (!WaitForRuntimeModules()) {
        logger::Write(L"Host validation failed: SDL.dll/Newton.dll did not load within 10 seconds.");
        return 3;
    }

    // The mapped image decides the adapter. The file name, timestamp, and
    // size below are support identifiers only.
    target::Selection selection{};
    if (!target::SelectAdapter(GetModuleHandleW(nullptr), selection)) {
        std::wostringstream rejected;
        rejected << L"Adapter selection failed closed (path=" << report.imagePath
                 << L", imageSize=0x" << std::hex << std::uppercase
                 << report.imageSize << L", timestamp=0x" << report.timestamp
                 << L"): " << selection.error;
        logger::Write(rejected.str());
        return 4;
    }
    const target::Adapter& adapter = *selection.adapter;
    const target::Mapping& mapping = selection.mapping;
    {
        const bool referenceBuild =
            report.timestamp == adapter.referenceTimestamp &&
            report.imageSize == adapter.referenceImageSize;
        std::wostringstream identity;
        identity << L"Adapter selected: " << adapter.name << L" (path="
                 << report.imagePath << L", base=0x" << std::hex
                 << std::uppercase << report.imageBase << L", imageSize=0x"
                 << report.imageSize << L", timestamp=0x" << report.timestamp
                 << L", reference build=" << BoolText(referenceBuild)
                 << L", signatures=" << std::dec
                 << (target::EngineSignatures().size() +
                     adapter.gameSignatures.size())
                 << L")";
        logger::Write(identity.str());
    }
    if (!target::SelectLayout(*adapter.layout)) {
        logger::Write(L"Layout selection failed closed: a different layout is already active.");
        return 4;
    }

    // Per-executable keys in [BlackPlague] or [Requiem] override [Features],
    // so a feature can be enabled for one game without the other.
    settings = LoadSettings(iniPath, adapter.overrideSection);
    logger::Write(std::wstring(L"Enabled=") + BoolText(settings.enabled) +
                  L", Diagnostics=" + BoolText(settings.diagnostics) +
                  L", Telemetry=" + BoolText(settings.telemetry) +
                  L", feature overrides=[" + adapter.overrideSection + L"]");
    if (settings.features.presentationLimiter || settings.targetFps != 0) {
        logger::Write(
            L"WARNING: PresentationLimiter/TargetFPS are not implemented; "
            L"use VSync or an external limiter.");
    }

    logger::Write(L"Initial loop signatures accepted: cGame::Run=" +
                  PointerText(mapping.gameRun) + L", cLogicTimer::WantUpdate=" +
                  PointerText(mapping.logicTimerWantUpdate) + L", cUpdater::Update=" +
                  PointerText(mapping.updaterUpdate) + L", cScene::Render=" +
                  PointerText(mapping.sceneRender));
    logger::Write(L"Presentation imports accepted: SDL_GetTicks=" +
                  PointerText(mapping.sdlGetTicks) + L", SDL_GL_SwapBuffers=" +
                  PointerText(mapping.sdlGlSwapBuffers));
    logger::Write(L"Cadence imports accepted: NewtonUpdate=" +
                  PointerText(mapping.newtonUpdate) + L", SDL_PollEvent=" +
                  PointerText(mapping.sdlPollEvent) +
                  L", SDL_GetRelativeMouseState=" +
                  PointerText(mapping.sdlGetRelativeMouseState) +
                  L", SDL_PumpEvents=" + PointerText(mapping.sdlPumpEvents));
    logger::Write(L"Camera mappings accepted: SetPosition=" +
                  PointerText(mapping.cameraSetPosition) + L", SetPitch=" +
                  PointerText(mapping.cameraSetPitch) + L", SetYaw=" +
                  PointerText(mapping.cameraSetYaw) + L", SetRoll=" +
                  PointerText(mapping.cameraSetRoll));
    logger::Write(L"Mouse mappings accepted: cMouseSDL::Update=" +
                  PointerText(mapping.mouseSdlUpdate) +
                  L", cButtonHandler::Update=" +
                  PointerText(mapping.buttonHandlerUpdate) +
                  L", cPlayer::AddYaw=" + PointerText(mapping.playerAddYaw) +
                  L", cPlayer::AddPitch=" +
                  PointerText(mapping.playerAddPitch));
    logger::Write(L"World-root mapping accepted: cNode3D::SetMatrix=" +
                  PointerText(mapping.nodeSetMatrix) +
                  L", cMeshEntity::GetModelMatrix=" +
                  PointerText(mapping.meshGetModelMatrix) +
                  L", iRenderable::GetInvModelMatrix=" +
                  PointerText(mapping.renderableGetInvModelMatrix));
    logger::Write(L"Hands-root mapping accepted: cPlayerHands::Update=" +
                  PointerText(mapping.playerHandsUpdate) +
                  L", iEntity3D::SetMatrix=" +
                  PointerText(mapping.entitySetMatrix));
    logger::Write(L"Skeletal-pose mappings accepted: cMeshEntity::UpdateGraphics=" +
                  PointerText(mapping.meshUpdateGraphics) +
                  L", cSubMeshEntity::UpdateGraphics=" +
                  PointerText(mapping.subMeshUpdateGraphics));
    logger::Write(L"Particle mapping accepted: iParticleEmitter3D::UpdateGraphics=" +
                  PointerText(mapping.particleEmitterUpdateGraphics));
    logger::Write(L"Post-effect mapping accepted: cRendererPostEffects::RenderImageTrail=" +
                  PointerText(mapping.rendererPostEffectsRenderImageTrail));
    logger::Write(L"Motion-blur mapping accepted: cRendererPostEffects::RenderMotionBlur=" +
                  PointerText(mapping.rendererPostEffectsRenderMotionBlur));
    if (!settings.telemetry && !settings.features.renderUnlock &&
        !settings.features.cameraInterpolation &&
        !settings.features.handsRootInterpolation &&
        !settings.features.worldRootInterpolation &&
        !settings.features.handsPoseInterpolation &&
        !settings.features.worldPoseInterpolation &&
        !settings.features.ragdollPoseInterpolation &&
        !settings.features.particleInterpolation &&
        !settings.features.imageTrailTiming &&
        !settings.features.motionBlurTiming &&
        !settings.features.mouseOverlay &&
        !settings.features.mouseAxisCorrection &&
        !settings.features.interactionCursorInterpolation &&
        !settings.features.guiMouseOverlay) {
        logger::Write(L"Diagnostic mapping complete. Telemetry is disabled; no hooks installed.");
        return 0;
    }

    std::wstring telemetryError;
    if (!telemetry::Install(mapping, settings.reportIntervalSeconds,
                            settings.telemetry, settings.features.renderUnlock,
                            settings.features.cameraInterpolation,
                            settings.features.handsRootInterpolation,
                            settings.features.worldRootInterpolation,
                            settings.features.handsPoseInterpolation,
                            settings.features.worldPoseInterpolation,
                            settings.features.ragdollPoseInterpolation,
                            settings.features.particleInterpolation,
                            settings.features.imageTrailTiming,
                            settings.features.motionBlurTiming,
                            settings.features.mouseOverlay,
                            settings.features.mouseAxisCorrection,
                            settings.features.interactionCursorInterpolation,
                            settings.features.guiMouseOverlay,
                            telemetryError)) {
        logger::Write(L"Telemetry hook transaction failed closed: " + telemetryError);
        return 5;
    }
    logger::Write(L"Hook transaction committed: render unlock=" +
                  BoolText(settings.features.renderUnlock) +
                  L", camera interpolation=" +
                  BoolText(settings.features.cameraInterpolation) +
                  L", hands-root interpolation=" +
                  BoolText(settings.features.handsRootInterpolation) +
                  L", world-root interpolation=" +
                  BoolText(settings.features.worldRootInterpolation) +
                  L", hands-pose interpolation=" +
                  BoolText(settings.features.handsPoseInterpolation) +
                  L", world-pose interpolation=" +
                  BoolText(settings.features.worldPoseInterpolation) +
                  L", ragdoll-pose interpolation=" +
                  BoolText(settings.features.ragdollPoseInterpolation) +
                  L", particle interpolation=" +
                  BoolText(settings.features.particleInterpolation) +
                  L", image-trail timing=" +
                  BoolText(settings.features.imageTrailTiming) +
                  L", motion-blur timing=" +
                  BoolText(settings.features.motionBlurTiming) +
                  L", mouse overlay=" +
                  BoolText(settings.features.mouseOverlay &&
                           settings.features.cameraInterpolation) +
                  L", mouse axis correction=" +
                  BoolText(settings.features.mouseAxisCorrection) +
                  L", interaction cursor interpolation=" +
                  BoolText(settings.features.interactionCursorInterpolation) +
                  L", GUI mouse overlay=" +
                  BoolText(settings.features.guiMouseOverlay) +
                  L".");
    return 0;
}

} // namespace pbpfps
