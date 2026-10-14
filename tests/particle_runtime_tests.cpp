#include "particle_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

constexpr std::ptrdiff_t kVectorBeginOffset = -0x68;
constexpr std::ptrdiff_t kVectorEndOffset = -0x64;
constexpr std::ptrdiff_t kVectorCapacityOffset = -0x60;
constexpr std::ptrdiff_t kActiveCountOffset = -0x5C;
constexpr std::ptrdiff_t kDirtyOffset = -0x04;
constexpr std::ptrdiff_t kDrawTypeOffset = 0x450;
constexpr std::ptrdiff_t kEmitterTypeOffset = 0x460;

struct Vector3 {
    float x;
    float y;
    float z;
};

struct Fixture;
Fixture* g_fixture = nullptr;

void Require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename T>
void Write(void* const base, const std::ptrdiff_t offset,
           const T& value) {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset,
                &value, sizeof(value));
}

template <typename T>
T Read(const void* const base, const std::ptrdiff_t offset) {
    T value{};
    std::memcpy(&value,
                static_cast<const std::uint8_t*>(base) + offset,
                sizeof(value));
    return value;
}

void __fastcall FakeUpdateGraphics(void* emitter, void*, void*, float, void*);

struct Fixture {
    alignas(void*) std::array<std::uint8_t, 0x700> storage{};
    alignas(float) std::array<std::uint8_t, 0x40> firstParticle{};
    alignas(float) std::array<std::uint8_t, 0x40> secondParticle{};
    std::array<void*, 2> particles{};
    std::array<void*, 21> vtable{};
    void* emitter = storage.data() + 0x100;
    int updateCalls = 0;
    std::uint8_t dirtyDuringUpdate = 0;
    std::array<Vector3, 2> positionsDuringUpdate{};
    Vector3 firstLastPositionDuringUpdate{};

    Fixture() {
        particles = {firstParticle.data(), secondParticle.data()};
        vtable[20] = reinterpret_cast<void*>(&FakeUpdateGraphics);
        Write(emitter, 0, vtable.data());
        Write(emitter, kVectorBeginOffset, particles.data());
        Write(emitter, kVectorEndOffset, particles.data() + particles.size());
        Write(emitter, kVectorCapacityOffset,
              particles.data() + particles.size());
        Write(emitter, kActiveCountOffset, std::uint32_t{2});
        Write(emitter, kDirtyOffset, std::uint8_t{0});
        Write(emitter, kDrawTypeOffset, std::uint32_t{0});
        Write(emitter, kEmitterTypeOffset, std::uint32_t{0});
        Write(firstParticle.data(), 0x00, Vector3{10.0F, 20.0F, 30.0F});
        Write(firstParticle.data(), 0x0C, Vector3{2.0F, 4.0F, 6.0F});
        Write(secondParticle.data(), 0x00, Vector3{-4.0F, 8.0F, 12.0F});
        Write(secondParticle.data(), 0x0C, Vector3{-8.0F, 4.0F, 0.0F});
    }
};

void __fastcall FakeUpdateGraphics(void* emitter, void*, void*, float,
                                   void*) {
    Require(g_fixture != nullptr && emitter == g_fixture->emitter,
            "update received wrong emitter");
    ++g_fixture->updateCalls;
    g_fixture->dirtyDuringUpdate =
        Read<std::uint8_t>(emitter, kDirtyOffset);
    g_fixture->positionsDuringUpdate[0] =
        Read<Vector3>(g_fixture->firstParticle.data(), 0x00);
    g_fixture->positionsDuringUpdate[1] =
        Read<Vector3>(g_fixture->secondParticle.data(), 0x00);
    g_fixture->firstLastPositionDuringUpdate =
        Read<Vector3>(g_fixture->firstParticle.data(), 0x0C);
    Write(emitter, kDirtyOffset, std::uint8_t{0});
}

pbpfps::clock::Sample Phase(const double alpha) {
    pbpfps::clock::Sample sample{};
    sample.kind = pbpfps::clock::SampleKind::interpolated;
    sample.alpha = alpha;
    return sample;
}

pbpfps::particle::Runtime MakeRuntime() {
    pbpfps::particle::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics),
                       reinterpret_cast<void*>(&FakeUpdateGraphics)});
    return runtime;
}

void TestInterpolationRegenerationAndExactRestore() {
    Fixture fixture;
    g_fixture = &fixture;
    auto runtime = MakeRuntime();
    runtime.BeginCollection();
    Require(runtime.Observe(fixture.emitter, fixture.emitter, 1.0F / 60.0F,
                            nullptr) ==
                pbpfps::particle::ObserveResult::added,
            "valid emitter was not collected");
    runtime.EndCollection();

    const auto result = runtime.Present(Phase(0.25));
    Require(result.targets == 1 && result.particles == 2 &&
                result.applied == 1 && result.regenerated == 1 &&
                result.invalid == 0 && result.restoreFailed == 0,
            "valid emitter did not complete presentation");
    Require(fixture.updateCalls == 1 && fixture.dirtyDuringUpdate == 1,
            "graphics regeneration was not forced exactly once");
    Require(std::abs(fixture.positionsDuringUpdate[0].x - 4.0F) < 1.0e-6F &&
                std::abs(fixture.positionsDuringUpdate[0].y - 8.0F) < 1.0e-6F &&
                std::abs(fixture.positionsDuringUpdate[0].z - 12.0F) < 1.0e-6F,
            "first particle was not interpolated");
    Require(std::abs(fixture.positionsDuringUpdate[1].x + 7.0F) < 1.0e-6F &&
                std::abs(fixture.positionsDuringUpdate[1].y - 5.0F) < 1.0e-6F &&
                std::abs(fixture.positionsDuringUpdate[1].z - 3.0F) < 1.0e-6F,
            "second particle was not interpolated");
    const Vector3 first = Read<Vector3>(fixture.firstParticle.data(), 0x00);
    const Vector3 second = Read<Vector3>(fixture.secondParticle.data(), 0x00);
    const Vector3 expectedFirst{10.0F, 20.0F, 30.0F};
    const Vector3 expectedSecond{-4.0F, 8.0F, 12.0F};
    Require(std::memcmp(&first, &expectedFirst, sizeof(first)) == 0 &&
                std::memcmp(&second, &expectedSecond, sizeof(second)) == 0 &&
                Read<std::uint8_t>(fixture.emitter, kDirtyOffset) == 0,
            "authoritative particle state was not restored exactly");
    g_fixture = nullptr;
}

// A line emitter draws each particle as a streak from its previous to its
// current position, so both ends are presented one-tick-displacement apart.
void TestLineEmitterMovesBothEndpoints() {
    Fixture fixture;
    g_fixture = &fixture;
    auto runtime = MakeRuntime();
    Write(fixture.emitter, kDrawTypeOffset, std::uint32_t{2});
    runtime.BeginCollection();
    Require(runtime.Observe(fixture.emitter, fixture.emitter, 1.0F / 60.0F,
                            nullptr) ==
                pbpfps::particle::ObserveResult::addedLine,
            "line emitter was not collected");
    runtime.EndCollection();

    const auto result = runtime.Present(Phase(0.25));
    Require(result.applied == 1 && result.regenerated == 1 &&
                result.invalid == 0 && result.restoreFailed == 0,
            "line emitter did not complete presentation");
    // Head at 2 + (10 - 2) * 0.25 = 4; tail one tick (8) behind it.
    Require(std::abs(fixture.positionsDuringUpdate[0].x - 4.0F) < 1.0e-6F &&
                std::abs(fixture.firstLastPositionDuringUpdate.x + 4.0F) <
                    1.0e-6F &&
                std::abs(fixture.firstLastPositionDuringUpdate.y + 8.0F) <
                    1.0e-6F &&
                std::abs(fixture.firstLastPositionDuringUpdate.z + 12.0F) <
                    1.0e-6F,
            "line streak endpoints were not presented together");
    const Vector3 head = Read<Vector3>(fixture.firstParticle.data(), 0x00);
    const Vector3 tail = Read<Vector3>(fixture.firstParticle.data(), 0x0C);
    const Vector3 expectedHead{10.0F, 20.0F, 30.0F};
    const Vector3 expectedTail{2.0F, 4.0F, 6.0F};
    Require(std::memcmp(&head, &expectedHead, sizeof(head)) == 0 &&
                std::memcmp(&tail, &expectedTail, sizeof(tail)) == 0,
            "line emitter endpoints were not restored exactly");
    g_fixture = nullptr;
}

// Point emitters must not have their previous position touched.
void TestPointEmitterLeavesPreviousPositionAlone() {
    Fixture fixture;
    g_fixture = &fixture;
    auto runtime = MakeRuntime();
    runtime.BeginCollection();
    (void)runtime.Observe(fixture.emitter, fixture.emitter, 0.0F, nullptr);
    runtime.EndCollection();
    (void)runtime.Present(Phase(0.25));
    Require(std::abs(fixture.firstLastPositionDuringUpdate.x - 2.0F) <
                1.0e-6F,
            "point emitter previous position was modified");
    g_fixture = nullptr;
}

// The engine builds quads in the view space of the camera it is handed, so
// particles that did not move still need fresh vertices every frame.
void TestStationaryParticlesStillRegenerate() {
    Fixture fixture;
    g_fixture = &fixture;
    Write(fixture.firstParticle.data(), 0x0C, Vector3{10.0F, 20.0F, 30.0F});
    Write(fixture.secondParticle.data(), 0x0C, Vector3{-4.0F, 8.0F, 12.0F});
    auto runtime = MakeRuntime();
    runtime.BeginCollection();
    (void)runtime.Observe(fixture.emitter, fixture.emitter, 0.0F, nullptr);
    runtime.EndCollection();
    const auto result = runtime.Present(Phase(0.5));
    Require(result.regenerated == 1 && result.stationarySkipped == 0 &&
                fixture.updateCalls == 1,
            "stationary particles were not regenerated for the camera");
    g_fixture = nullptr;
}

void TestBeamEmittersAreExcluded() {
    Fixture fixture;
    auto runtime = MakeRuntime();
    runtime.BeginCollection();
    Write(fixture.emitter, kEmitterTypeOffset, std::uint32_t{1});
    Require(runtime.Observe(fixture.emitter, fixture.emitter, 0.0F, nullptr) ==
                pbpfps::particle::ObserveResult::ignoredBeam,
            "beam emitter was accepted");
}

void TestInvalidAndUnavailableInputsFailClosed() {
    Fixture fixture;
    g_fixture = &fixture;
    auto runtime = MakeRuntime();
    runtime.BeginCollection();
    Require(runtime.Observe(fixture.emitter, fixture.emitter, 0.0F, nullptr) ==
                pbpfps::particle::ObserveResult::added,
            "valid emitter was not collected for fail-closed test");
    runtime.EndCollection();
    auto result = runtime.Present({});
    Require(result.clockWait == 1 && fixture.updateCalls == 0,
            "unavailable clock changed particle presentation");

    Write(fixture.firstParticle.data(), 0x00,
          Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    result = runtime.Present(Phase(0.5));
    Require(result.invalid == 1 && fixture.updateCalls == 0,
            "invalid particle position reached graphics regeneration");
    g_fixture = nullptr;
}

void TestStaleParticlePointerFailsClosed() {
    Fixture fixture;
    g_fixture = &fixture;
    auto runtime = MakeRuntime();
    runtime.BeginCollection();
    Require(runtime.Observe(fixture.emitter, fixture.emitter, 0.0F, nullptr) ==
                pbpfps::particle::ObserveResult::added,
            "valid emitter was not collected for stale-pointer test");
    runtime.EndCollection();

    // The first pages are never mapped, so this stands in for a freed
    // particle.
    fixture.particles[1] = reinterpret_cast<void*>(std::uintptr_t{0x1000});
    const auto result = runtime.Present(Phase(0.5));
    Require(result.invalid == 1 && result.applied == 0 &&
                result.restoreFailed == 0 && fixture.updateCalls == 0,
            "stale particle pointer reached graphics regeneration");
    const Vector3 first = Read<Vector3>(fixture.firstParticle.data(), 0x00);
    const Vector3 expectedFirst{10.0F, 20.0F, 30.0F};
    Require(std::memcmp(&first, &expectedFirst, sizeof(first)) == 0,
            "rejected emitter left a particle in its temporary position");

    // An emitter pointer that is itself unmapped is rejected at collection.
    runtime.BeginCollection();
    Require(runtime.Observe(reinterpret_cast<void*>(std::uintptr_t{0x1000}),
                            fixture.emitter, 0.0F, nullptr) ==
                pbpfps::particle::ObserveResult::invalid,
            "unmapped emitter was collected");
    runtime.EndCollection();
    g_fixture = nullptr;
}

} // namespace

int main() {
    TestInterpolationRegenerationAndExactRestore();
    TestLineEmitterMovesBothEndpoints();
    TestPointEmitterLeavesPreviousPositionAlone();
    TestStationaryParticlesStillRegenerate();
    TestBeamEmittersAreExcluded();
    TestInvalidAndUnavailableInputsFailClosed();
    TestStaleParticlePointerFailsClosed();
    std::cout << "particle runtime tests passed\n";
    return EXIT_SUCCESS;
}
