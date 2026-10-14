#include "pose_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename T, std::size_t Size>
void Write(std::array<std::uint8_t, Size>& bytes,
           const std::size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

pbpfps::transform::Matrix Translation(const float x) {
    auto matrix = pbpfps::transform::Identity();
    matrix.values[3] = x;
    return matrix;
}

pbpfps::clock::Sample Pair(const std::uint64_t older,
                          const std::uint64_t newer,
                          const double alpha) {
    pbpfps::clock::Sample sample{};
    sample.kind = pbpfps::clock::SampleKind::interpolated;
    sample.generation = 1;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = alpha;
    return sample;
}

std::uint32_t g_regenerations = 0;
float g_lastRegeneratedX = 0.0F;

void __fastcall FakeSubMeshUpdate(void* self, void*, void*, float, void*) {
    ++g_regenerations;
    void* owner = nullptr;
    std::memcpy(&owner, static_cast<std::uint8_t*>(self) + 0x3FC,
                sizeof(owner));
    pbpfps::transform::Matrix* bones = nullptr;
    std::memcpy(&bones, static_cast<std::uint8_t*>(owner) + 0x46C,
                sizeof(bones));
    g_lastRegeneratedX = bones[0].values[3];
}

struct Fixture {
    std::array<std::uint8_t, 0x500> mesh{};
    std::array<std::uint8_t, 0x500> subMesh{};
    std::array<void*, 32> meshVtable{};
    std::array<void*, 32> subMeshVtable{};
    std::array<pbpfps::transform::Matrix, 4> bones{};
    void* meshUpdateTarget = reinterpret_cast<void*>(0x1000);
    void* subMeshUpdateTarget = reinterpret_cast<void*>(0x2000);

    Fixture() {
        meshVtable[20] = meshUpdateTarget;
        subMeshVtable[20] = subMeshUpdateTarget;
        void* meshTable = meshVtable.data();
        void* subMeshTable = subMeshVtable.data();
        Write(mesh, 0x00, meshTable);
        Write(subMesh, 0x00, subMeshTable);
        auto* begin = bones.data();
        auto* end = bones.data() + 2;
        auto* capacity = bones.data() + bones.size();
        Write(mesh, 0x46C, begin);
        Write(mesh, 0x470, end);
        Write(mesh, 0x474, capacity);
        const std::uint8_t falseValue = 0;
        Write(mesh, 0x478, falseValue);
        Write(mesh, 0x484, falseValue);
        void* owner = mesh.data();
        void* dynamicBuffer = reinterpret_cast<void*>(0x3000);
        Write(subMesh, 0x3FC, owner);
        Write(subMesh, 0x40C, dynamicBuffer);
        Write(subMesh, 0x425, falseValue);
    }

    void Set(const float base) {
        bones[0] = Translation(base);
        bones[1] = Translation(base + 1.0F);
    }
};

void ConfigureRuntime(pbpfps::pose::Runtime& runtime,
                      const Fixture& fixture,
                      const bool ragdoll = false) {
    runtime.Configure(
        {reinterpret_cast<void*>(&FakeSubMeshUpdate),
         fixture.meshUpdateTarget, fixture.subMeshUpdateTarget},
        {false, true, ragdoll});
}

void Observe(pbpfps::pose::Runtime& runtime, Fixture& fixture,
             const std::uint64_t serial) {
    runtime.BeginCollection(serial, 1);
    const auto meshResult = runtime.ObserveMesh(fixture.mesh.data());
    Require(meshResult == pbpfps::pose::ObserveMeshResult::captured ||
                meshResult == pbpfps::pose::ObserveMeshResult::historyReset,
            "skeletal mesh palette was not captured");
    Require(runtime.ObserveSubMesh(fixture.subMesh.data(), nullptr, 0.0F,
                                   nullptr) ==
                pbpfps::pose::ObserveSubMeshResult::added,
            "CPU-skinned submesh was not collected");
    runtime.EndCollection();
}

void TestOverrideRegenerateAndRestore() {
    Fixture fixture;
    pbpfps::pose::Runtime runtime;
    ConfigureRuntime(runtime, fixture);
    fixture.Set(0.0F);
    Observe(runtime, fixture, 1);
    auto first = runtime.BeginRender(Pair(1, 2, 0.5));
    Require(first.applied == 0 && first.pairMissing == 1,
            "first palette unexpectedly interpolated");
    (void)runtime.EndRender();

    fixture.Set(2.0F);
    Observe(runtime, fixture, 2);
    const std::uint32_t before = g_regenerations;
    const auto applied = runtime.BeginRender(Pair(1, 2, 0.5));
    Require(applied.applied == 1 && applied.regenerated == 1 &&
                g_regenerations == before + 1 &&
                std::abs(fixture.bones[0].values[3] - 1.0F) < 1.0e-5F &&
                std::abs(g_lastRegeneratedX - 1.0F) < 1.0e-5F,
            "interpolated palette did not regenerate the CPU skin");
    const auto restored = runtime.EndRender();
    Require(restored.restoreFailed == 0 && restored.regenerated == 1 &&
                std::abs(fixture.bones[0].values[3] - 2.0F) < 1.0e-5F &&
                std::abs(g_lastRegeneratedX - 2.0F) < 1.0e-5F,
            "authoritative palette and CPU skin were not restored");
}

void TestClassificationAndIdentityGates() {
    Fixture fixture;
    pbpfps::pose::Runtime runtime;
    ConfigureRuntime(runtime, fixture);
    const std::uint8_t trueValue = 1;
    Write(fixture.mesh, 0x478, trueValue);
    runtime.BeginCollection(1, 1);
    Require(runtime.ObserveMesh(fixture.mesh.data()) ==
                pbpfps::pose::ObserveMeshResult::ignored,
            "ragdoll entered the disabled world-pose path");
    runtime.EndCollection();

    Write(fixture.mesh, 0x478, static_cast<std::uint8_t>(0));
    fixture.Set(0.0F);
    Observe(runtime, fixture, 1);
    fixture.Set(2.0F);
    Observe(runtime, fixture, 2);
    auto* movedBegin = fixture.bones.data() + 1;
    auto* movedEnd = fixture.bones.data() + 3;
    Write(fixture.mesh, 0x46C, movedBegin);
    Write(fixture.mesh, 0x470, movedEnd);
    const auto batch = runtime.BeginRender(Pair(1, 2, 0.5));
    Require(batch.applied == 0 && batch.invalid == 1,
            "palette-buffer identity change did not fail closed");
    (void)runtime.EndRender();
}

void TestStationaryRagdollLeavesHotPath() {
    Fixture fixture;
    pbpfps::pose::Runtime runtime;
    ConfigureRuntime(runtime, fixture, true);
    const std::uint8_t trueValue = 1;
    Write(fixture.mesh, 0x478, trueValue);
    fixture.Set(0.0F);
    Observe(runtime, fixture, 1);
    Observe(runtime, fixture, 2);
    auto batch = runtime.BeginRender(Pair(1, 2, 0.5));
    Require(batch.targets == 0 && batch.applied == 0 &&
                batch.stationarySkipped == 1,
            "stationary ragdoll remained in the regeneration hot path");
    (void)runtime.EndRender();

    fixture.Set(2.0F);
    Observe(runtime, fixture, 3);
    batch = runtime.BeginRender(Pair(2, 3, 0.5));
    Require(batch.targets == 1 && batch.applied == 1 &&
                batch.stationarySkipped == 0,
            "moving ragdoll did not resume pose interpolation");
    (void)runtime.EndRender();
}

} // namespace

int main() {
    TestOverrideRegenerateAndRestore();
    TestClassificationAndIdentityGates();
    TestStationaryRagdollLeavesHotPath();
    std::cout << "pose runtime tests passed\n";
    return EXIT_SUCCESS;
}
