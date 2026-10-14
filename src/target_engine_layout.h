#pragma once

#include "target_layout.h"

namespace pbpfps::target {

// HPL1 engine-layer fields shared by Black Plague and Requiem. Every value
// is read by a mapped engine function whose complete body is identical in
// both executables (and to the validated Overture build), or lies inside a
// class whose whole vtable compares identical. Game-layer fields are left
// for the per-executable tables to fill.
[[nodiscard]] constexpr Layout EngineLayout() noexcept {
    Layout layout{};
    layout.gameLimitFps = 0x28;
    layout.timerNextDeadline = 0x00;
    layout.timerStep = 0x08;

    layout.sceneActiveCamera = 0x64;
    layout.cameraPosition = 0x04;
    layout.cameraPitch = 0x20;
    layout.cameraYaw = 0x24;
    layout.cameraRoll = 0x28;
    layout.cameraPitchHigh = 0x2C;
    layout.cameraPitchLow = 0x30;
    layout.cameraYawHigh = 0x34;
    layout.cameraYawLow = 0x38;
    layout.cameraRotateMode = 0x3C;
    layout.cameraGetTypeVtableIndex = 3;
    layout.cameraEulerAnglesMode = 0;
    layout.cameraType3D = 1;

    layout.nodeLocalMatrix = 0x68;
    layout.entityLocalMatrix = 0x34;
    layout.meshGetModelMatrixVtableIndex = 18;
    layout.meshModelMatrixCache = 0x4D0;
    layout.updateGraphicsVtableIndex = 20;
    layout.meshBoneMatricesBegin = 0x46C;
    layout.meshBoneMatricesEnd = 0x470;
    layout.meshBoneMatricesCapacity = 0x474;
    layout.meshSkeletonPhysicsActive = 0x478;
    layout.meshSkeletonPhysicsSleeping = 0x484;

    layout.subMeshOwner = 0x3FC;
    layout.subMeshDynamicVertexBuffer = 0x40C;
    layout.subMeshGraphicsUpdated = 0x425;

    layout.emitterParticlesBegin = -0x68;
    layout.emitterParticlesEnd = -0x64;
    layout.emitterParticlesCapacity = -0x60;
    layout.emitterActiveCount = -0x5C;
    layout.emitterGraphicsDirty = -0x04;
    layout.emitterDrawType = 0x450;
    layout.emitterType = 0x460;
    layout.particlePosition = 0x00;
    layout.particleLastPosition = 0x0C;
    layout.emitterLineDrawType = 2;
    layout.emitterBeamType = 1;

    layout.imageTrailActive = 0x28;
    layout.imageTrailFirstPass = 0x29;
    layout.imageTrailAmount = 0x30;
    layout.motionBlurVertexProgram = 0x50;
    layout.motionBlurFragmentProgram = 0x54;
    layout.motionBlurActive = 0x7C;
    layout.motionBlurScale = 0x80;
    layout.motionBlurFirstPass = 0x84;

    layout.mouseLowLevelGraphics = 0x64;
    layout.graphicsGetScreenSizeVtableIndex = 5;

    layout.initHasHaptics = kAbsent;
    layout.initGameGui = kAbsent;
    return layout;
}

} // namespace pbpfps::target
