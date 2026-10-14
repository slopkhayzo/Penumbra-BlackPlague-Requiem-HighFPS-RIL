#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace pbpfps::target {

struct Mapping {
    void* gameRun = nullptr;
    void* logicTimerReset = nullptr;
    void* logicTimerEndUpdateLoop = nullptr;
    void* logicTimerGetStepSize = nullptr;
    void* logicTimerWantUpdate = nullptr;
    void* updaterUpdate = nullptr;
    void* updaterOnDraw = nullptr;
    void* updaterOnPostBufferSwap = nullptr;
    void* sceneUpdateRenderList = nullptr;
    void* sceneRender = nullptr;
    void* cameraSetPosition = nullptr;
    void* cameraSetPitch = nullptr;
    void* cameraSetYaw = nullptr;
    void* cameraSetRoll = nullptr;
    void* nodeSetMatrix = nullptr;
    void* playerHandsUpdate = nullptr;
    void* entitySetMatrix = nullptr;
    void* meshGetModelMatrix = nullptr;
    void* meshUpdateGraphics = nullptr;
    void* subMeshUpdateGraphics = nullptr;
    void* particleEmitterUpdateGraphics = nullptr;
    void* renderableGetInvModelMatrix = nullptr;
    void* rendererPostEffectsRenderMotionBlur = nullptr;
    void* rendererPostEffectsRenderImageTrail = nullptr;
    void* buttonHandlerUpdate = nullptr;
    void* mouseSdlUpdate = nullptr;
    void* playerAddYaw = nullptr;
    void* playerAddPitch = nullptr;
    void* sdlGetTicks = nullptr;
    void* sdlGlSwapBuffers = nullptr;
    void* sdlPollEvent = nullptr;
    void* sdlGetRelativeMouseState = nullptr;
    void* sdlPumpEvents = nullptr;
    void* newtonUpdate = nullptr;
};

struct Adapter;

// A validated view of a mapped PE32 image and its executable section.
struct ImageView {
    const std::uint8_t* base = nullptr;
    std::uint32_t imageSize = 0;
    std::uint32_t timestamp = 0;
    const std::uint8_t* text = nullptr;
    std::size_t textSize = 0;
    std::uint32_t importDirectoryRva = 0;
    std::uint32_t importDirectorySize = 0;
};

[[nodiscard]] bool OpenImage(HMODULE executable, ImageView& image,
                             std::wstring& error) noexcept;

// Resolves every engine and game signature of one adapter against the image.
// All-or-nothing: on failure the mapping is cleared and error names the first
// signature that did not match as required.
[[nodiscard]] bool ResolveSignatures(const ImageView& image,
                                     const Adapter& adapter, Mapping& mapping,
                                     std::wstring& error) noexcept;

// Resolves the SDL and Newton entry points from the bound import table.
[[nodiscard]] bool ResolveImports(const ImageView& image, Mapping& mapping,
                                  std::wstring& error) noexcept;

} // namespace pbpfps::target
