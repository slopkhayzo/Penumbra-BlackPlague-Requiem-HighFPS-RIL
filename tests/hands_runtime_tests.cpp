#include "hands_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::size_t kEntityLocalMatrix = 0x34;
constexpr std::size_t kGetModelMatrixVtableIndex = 18;
constexpr std::size_t kCurrentHudModels = 0x6C;
constexpr std::size_t kCurrentHudModelCount = 0x74;
constexpr std::size_t kHudModelEntity = 0x118;

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

pbpfps::transform::Matrix Translation(const float x) {
    pbpfps::transform::Matrix result = pbpfps::transform::Identity();
    result.values[3] = x;
    return result;
}

void MeshModelTarget() {}

struct FakeEntity {
    std::array<void*, 32> vtable{};
    alignas(void*) std::array<std::byte, 0x200> storage{};

    FakeEntity() {
        *reinterpret_cast<void***>(storage.data()) = vtable.data();
        vtable[kGetModelMatrixVtableIndex] =
            reinterpret_cast<void*>(&MeshModelTarget);
    }
};

struct FakeHandsState {
    alignas(void*) std::array<std::byte, 0x100> hands{};
    alignas(void*) std::array<std::byte, 0x180> model{};

    explicit FakeHandsState(void* entity) {
        const std::int32_t count = 2;
        void* modelPointer = model.data();
        std::memcpy(hands.data() + kCurrentHudModels, &modelPointer,
                    sizeof(modelPointer));
        std::memcpy(hands.data() + kCurrentHudModelCount, &count,
                    sizeof(count));
        std::memcpy(model.data() + kHudModelEntity, &entity, sizeof(entity));
    }
};

void WriteMatrix(void* entity, const pbpfps::transform::Matrix& matrix) {
    std::memcpy(static_cast<std::byte*>(entity) + kEntityLocalMatrix, &matrix,
                sizeof(matrix));
}

pbpfps::transform::Matrix ReadMatrix(const void* entity) {
    pbpfps::transform::Matrix result{};
    std::memcpy(&result,
                static_cast<const std::byte*>(entity) + kEntityLocalMatrix,
                sizeof(result));
    return result;
}

void __fastcall SetMatrix(void* self, void*,
                          const pbpfps::transform::Matrix& matrix) {
    WriteMatrix(self, matrix);
}

pbpfps::clock::Sample Phase(const double alpha, const std::uint64_t older = 1,
                           const std::uint64_t newer = 2) {
    pbpfps::clock::Sample result{};
    result.kind = pbpfps::clock::SampleKind::interpolated;
    result.generation = 7;
    result.olderSerial = older;
    result.newerSerial = newer;
    result.alpha = alpha;
    return result;
}

void TestApplyRestore() {
    FakeEntity entity;
    FakeHandsState hands(entity.storage.data());
    pbpfps::hands::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix),
                      reinterpret_cast<void*>(&MeshModelTarget));

    const auto older = Translation(10.0F);
    const auto newer = Translation(12.0F);
    WriteMatrix(entity.storage.data(), older);
    Require(runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                    older, 1) ==
                pbpfps::hands::NotifyResult::staged,
            "first hands root was not staged");
    Require(runtime.CommitCompletedTick(1, 7).reset == 1,
            "first hands endpoint did not establish generation");
    WriteMatrix(entity.storage.data(), newer);
    (void)runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                  newer, 2);
    Require(runtime.CommitCompletedTick(2, 7).captured == 1,
            "second hands endpoint was not captured");

    const auto begun = runtime.BeginRender(Phase(0.25));
    Require(begun.ready == 1 && begun.applyFailed == 0,
            "hands root was not render-ready");
    Require(std::abs(ReadMatrix(entity.storage.data()).values[3] - 10.5F) <
                1.0e-5F,
            "interpolated hands root was not applied");
    Require(runtime.EndRender().restoreFailed == 0,
            "hands root restoration failed");
    Require(ReadMatrix(entity.storage.data()).values[3] == 12.0F,
            "authoritative hands root was not restored exactly");
}

void TestPresentationCorrectionAndExactRestore() {
    FakeEntity entity;
    FakeHandsState hands(entity.storage.data());
    pbpfps::hands::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix),
                      reinterpret_cast<void*>(&MeshModelTarget));

    const auto older = Translation(10.0F);
    const auto newer = Translation(12.0F);
    WriteMatrix(entity.storage.data(), older);
    (void)runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                  older, 1);
    (void)runtime.CommitCompletedTick(1, 7);
    WriteMatrix(entity.storage.data(), newer);
    (void)runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                  newer, 2);
    (void)runtime.CommitCompletedTick(2, 7);

    const auto correction = Translation(3.0F);
    Require(runtime.SetPresentationCorrection(&correction),
            "valid hands presentation correction was rejected");
    Require(runtime.BeginRender(Phase(0.25)).ready == 1,
            "corrected hands root was not render-ready");
    Require(std::abs(ReadMatrix(entity.storage.data()).values[3] - 13.5F) <
                1.0e-5F,
            "camera presentation correction was not composed onto hands");
    Require(runtime.EndRender().restoreFailed == 0 &&
                ReadMatrix(entity.storage.data()).values[3] == 12.0F,
            "corrected hands root did not restore authoritative state");

    Require(runtime.SetPresentationCorrection(nullptr),
            "hands presentation correction could not be cleared");
    Require(runtime.BeginRender(Phase(0.25)).ready == 1,
            "uncorrected hands root was not render-ready");
    Require(std::abs(ReadMatrix(entity.storage.data()).values[3] - 10.5F) <
                1.0e-5F,
            "cleared hands correction leaked into the next render");
    (void)runtime.EndRender();
}

void TestMissingRootIsRetiredImmediately() {
    FakeEntity entity;
    FakeHandsState hands(entity.storage.data());
    pbpfps::hands::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix),
                      reinterpret_cast<void*>(&MeshModelTarget));
    const auto first = Translation(0.0F);
    const auto second = Translation(1.0F);
    WriteMatrix(entity.storage.data(), first);
    (void)runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                  first, 1);
    (void)runtime.CommitCompletedTick(1, 7);
    WriteMatrix(entity.storage.data(), second);
    (void)runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                  second, 2);
    (void)runtime.CommitCompletedTick(2, 7);

    // cPlayerHands writes every live HUD root each tick. Absence means an
    // unequip/model replacement may have destroyed the object.
    (void)runtime.CommitCompletedTick(3, 7);
    Require(runtime.BeginRender(Phase(0.5, 2, 3)).ready == 0,
            "missing hands root was retained after its lifetime ended");
    (void)runtime.EndRender();
}

void TestReplacedRootFailsClosedBeforeRender() {
    FakeEntity entity;
    FakeHandsState hands(entity.storage.data());
    pbpfps::hands::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix),
                      reinterpret_cast<void*>(&MeshModelTarget));
    const auto first = Translation(0.0F);
    const auto second = Translation(1.0F);
    WriteMatrix(entity.storage.data(), first);
    (void)runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                  first, 1);
    (void)runtime.CommitCompletedTick(1, 7);
    WriteMatrix(entity.storage.data(), second);
    (void)runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                  second, 2);
    (void)runtime.CommitCompletedTick(2, 7);

    void* noModel = nullptr;
    std::memcpy(hands.hands.data() + kCurrentHudModels, &noModel,
                sizeof(noModel));
    const auto result = runtime.BeginRender(Phase(0.5));
    Require(result.invalid == 1 && result.ready == 0,
            "replaced hands root was touched after losing ownership");
    (void)runtime.EndRender();
    Require(ReadMatrix(entity.storage.data()).values[3] == 1.0F,
            "replaced hands root changed on the fail-closed path");
}

void TestWrongEntityTypeFailsClosed() {
    FakeEntity entity;
    FakeHandsState hands(entity.storage.data());
    entity.vtable[kGetModelMatrixVtableIndex] = nullptr;
    pbpfps::hands::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix),
                      reinterpret_cast<void*>(&MeshModelTarget));
    const auto matrix = Translation(0.0F);
    WriteMatrix(entity.storage.data(), matrix);
    Require(runtime.NotifyRootMoved(hands.hands.data(), entity.storage.data(),
                                    matrix, 1) ==
                pbpfps::hands::NotifyResult::invalid,
            "non-mesh entity passed the identity guard");
}

void TestUnrelatedMeshIsIgnored() {
    FakeEntity currentEntity;
    FakeEntity unrelatedEntity;
    FakeHandsState hands(currentEntity.storage.data());
    pbpfps::hands::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix),
                      reinterpret_cast<void*>(&MeshModelTarget));
    const auto matrix = Translation(0.0F);
    WriteMatrix(unrelatedEntity.storage.data(), matrix);
    Require(runtime.NotifyRootMoved(hands.hands.data(),
                                    unrelatedEntity.storage.data(), matrix,
                                    1) == pbpfps::hands::NotifyResult::ignored,
            "unrelated mesh write was not ignored by the ownership guard");
}

} // namespace

int main() {
    TestApplyRestore();
    TestPresentationCorrectionAndExactRestore();
    TestMissingRootIsRetiredImmediately();
    TestReplacedRootFailsClosedBeforeRender();
    TestWrongEntityTypeFailsClosed();
    TestUnrelatedMeshIsIgnored();
    std::cout << "hands runtime tests passed\n";
    return EXIT_SUCCESS;
}
