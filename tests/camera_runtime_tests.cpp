#include "camera_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::size_t kSceneCamera = 0x64;
constexpr std::size_t kPosition = 0x04;
constexpr std::size_t kPitch = 0x20;
constexpr std::size_t kYaw = 0x24;
constexpr std::size_t kRoll = 0x28;
constexpr std::size_t kRotateMode = 0x3C;

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename T>
void Write(void* base, const std::size_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof(value));
}

template <typename T>
T Read(const void* base, const std::size_t offset) {
    T result{};
    std::memcpy(&result, static_cast<const std::byte*>(base) + offset,
                sizeof(result));
    return result;
}

int __fastcall CameraType3D(void*, void*) { return 1; }
int __fastcall CameraType2D(void*, void*) { return 0; }

void __fastcall SetPosition(void* self, void*,
                            const pbpfps::camera::Vec3& position) {
    Write(self, kPosition, position);
}

void __fastcall SetPitch(void* self, void*, const float value) {
    Write(self, kPitch, value);
}

void __fastcall SetYaw(void* self, void*, const float value) {
    Write(self, kYaw, value);
}

void __fastcall SetRoll(void* self, void*, const float value) {
    Write(self, kRoll, value);
}

pbpfps::clock::Sample Phase(const std::uint64_t older,
                           const std::uint64_t newer, const double alpha) {
    pbpfps::clock::Sample result{};
    result.kind = pbpfps::clock::SampleKind::interpolated;
    result.generation = 7;
    result.olderSerial = older;
    result.newerSerial = newer;
    result.alpha = alpha;
    return result;
}

void SetPose(void* camera, const pbpfps::camera::Pose& pose) {
    Write(camera, kPosition, pose.position);
    Write(camera, kPitch, pose.pitch);
    Write(camera, kYaw, pose.yaw);
    Write(camera, kRoll, pose.roll);
    Write(camera, kRotateMode, std::uint32_t{0});
}

void TestApplyAndExactRestore() {
    alignas(void*) std::array<std::byte, 0x80> scene{};
    alignas(void*) std::array<std::byte, 0x900> camera{};
    std::array<void*, 4> vtable{};
    vtable[3] = reinterpret_cast<void*>(&CameraType3D);
    void* vtablePointer = vtable.data();
    Write(camera.data(), 0, vtablePointer);
    void* cameraPointer = camera.data();
    Write(scene.data(), kSceneCamera, cameraPointer);

    pbpfps::camera::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&SetPosition),
                       reinterpret_cast<void*>(&SetPitch),
                       reinterpret_cast<void*>(&SetYaw),
                       reinterpret_cast<void*>(&SetRoll)});

    const pbpfps::camera::Pose older{{0.0F, 2.0F, 4.0F}, 0.0F, 0.1F, 0.0F};
    const pbpfps::camera::Pose newer{{2.0F, 4.0F, 6.0F}, 0.2F, 0.5F, 0.1F};
    SetPose(camera.data(), older);
    Require(runtime.Apply(scene.data(), 1, Phase(1, 2, 0.25)) ==
                pbpfps::camera::FrameResult::identityChanged,
            "first camera observation did not establish identity");

    SetPose(camera.data(), newer);
    Require(runtime.CaptureCompletedTick(2, 7) ==
                pbpfps::camera::FrameResult::captured,
            "completed fixed tick was not captured");
    Require(runtime.Apply(scene.data(), 2, Phase(1, 2, 0.25)) ==
                pbpfps::camera::FrameResult::applied,
            "valid serial pair was not applied");
    const auto interpolated =
        Read<pbpfps::camera::Vec3>(camera.data(), kPosition);
    Require(std::abs(interpolated.x - 0.5F) < 1.0e-5F &&
                std::abs(interpolated.y - 2.5F) < 1.0e-5F &&
                std::abs(interpolated.z - 4.5F) < 1.0e-5F,
            "runtime applied the wrong interpolated position");
    pbpfps::camera::Pose previewBefore{};
    pbpfps::camera::Pose previewAfter{};
    Require(runtime.ApplyAngularPreview(0.03F, -0.07F, &previewBefore,
                                        &previewAfter),
            "valid angular preview was not applied");
    Require(std::abs(previewBefore.pitch - 0.05F) < 1.0e-5F &&
                std::abs(previewBefore.yaw - 0.20F) < 1.0e-5F &&
                std::abs(previewAfter.pitch - 0.08F) < 1.0e-5F &&
                std::abs(previewAfter.yaw - 0.13F) < 1.0e-5F,
            "angular preview did not report its exact before/after poses");
    Require(std::abs(Read<float>(camera.data(), kPitch) - 0.08F) < 1.0e-5F &&
                std::abs(Read<float>(camera.data(), kYaw) - 0.13F) < 1.0e-5F,
            "angular preview was not composed over the interpolated camera");
    Require(runtime.Restore() == pbpfps::camera::FrameResult::restored,
            "runtime could not restore the authoritative pose");
    const auto restored = Read<pbpfps::camera::Vec3>(camera.data(), kPosition);
    Require(restored.x == newer.position.x && restored.y == newer.position.y &&
                restored.z == newer.position.z &&
                Read<float>(camera.data(), kPitch) == newer.pitch &&
                Read<float>(camera.data(), kYaw) == newer.yaw &&
                Read<float>(camera.data(), kRoll) == newer.roll,
            "authoritative camera state was not restored exactly");
    Require(!runtime.ApplyAngularPreview(0.01F, 0.01F),
            "angular preview remained active after restoration");
}

void TestRejects2DCamera() {
    alignas(void*) std::array<std::byte, 0x80> scene{};
    alignas(void*) std::array<std::byte, 0x900> camera{};
    std::array<void*, 4> vtable{};
    vtable[3] = reinterpret_cast<void*>(&CameraType2D);
    void* vtablePointer = vtable.data();
    Write(camera.data(), 0, vtablePointer);
    void* cameraPointer = camera.data();
    Write(scene.data(), kSceneCamera, cameraPointer);

    pbpfps::camera::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&SetPosition),
                       reinterpret_cast<void*>(&SetPitch),
                       reinterpret_cast<void*>(&SetYaw),
                       reinterpret_cast<void*>(&SetRoll)});
    Require(runtime.Apply(scene.data(), 1, Phase(1, 2, 0.5)) ==
                pbpfps::camera::FrameResult::notCamera3D,
            "2D camera was accepted for 3D pose interpolation");
}

} // namespace

int main() {
    TestApplyAndExactRestore();
    TestRejects2DCamera();
    std::cout << "camera runtime tests passed\n";
    return EXIT_SUCCESS;
}
