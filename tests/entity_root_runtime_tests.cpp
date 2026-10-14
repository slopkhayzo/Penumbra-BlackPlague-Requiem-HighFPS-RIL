#include "entity_root_runtime.h"

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
    auto result = pbpfps::transform::Identity();
    result.values[3] = x;
    return result;
}

pbpfps::clock::Sample Pair(const std::uint64_t older,
                          const std::uint64_t newer,
                          const double alpha) {
    pbpfps::clock::Sample result{};
    result.kind = pbpfps::clock::SampleKind::interpolated;
    result.generation = 1;
    result.olderSerial = older;
    result.newerSerial = newer;
    result.alpha = alpha;
    return result;
}

void __fastcall FakeSetMatrix(void* self, void*,
                              const pbpfps::transform::Matrix& matrix) {
    std::memcpy(static_cast<std::uint8_t*>(self) + 0x34, &matrix,
                sizeof(matrix));
}

struct Fixture {
    std::array<std::uint8_t, 0x100> entity{};
    std::array<void*, 32> vtable{};
    void* getModelMatrixTarget = reinterpret_cast<void*>(0x1234);

    Fixture() {
        vtable[18] = getModelMatrixTarget;
        void* table = vtable.data();
        Write(entity, 0, table);
        Set(0.0F);
    }

    void Set(const float x) {
        const auto matrix = Translation(x);
        Write(entity, 0x34, matrix);
    }

    float X() const {
        pbpfps::transform::Matrix matrix{};
        std::memcpy(&matrix, entity.data() + 0x34, sizeof(matrix));
        return matrix.values[3];
    }
};

void TestGameplayRootOverrideAndRestore() {
    Fixture fixture;
    pbpfps::entity_root::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&FakeSetMatrix),
                      fixture.getModelMatrixTarget);
    auto first = Translation(0.0F);
    Require(runtime.NotifyMoved(fixture.entity.data(), first, 1) ==
                pbpfps::entity_root::NotifyResult::staged,
            "first gameplay root was not staged");
    (void)runtime.CommitCompletedTick(1, 1);
    auto second = Translation(2.0F);
    fixture.Set(2.0F);
    Require(runtime.NotifyMoved(fixture.entity.data(), second, 2) ==
                pbpfps::entity_root::NotifyResult::staged,
            "second gameplay root was not staged");
    (void)runtime.CommitCompletedTick(2, 1);
    const auto applied = runtime.BeginRender(Pair(1, 2, 0.5));
    Require(applied.ready == 1 && std::abs(fixture.X() - 1.0F) < 1.0e-5F,
            "gameplay root was not interpolated");
    const auto restored = runtime.EndRender();
    Require(restored.restoreFailed == 0 &&
                std::abs(fixture.X() - 2.0F) < 1.0e-5F,
            "gameplay root was not restored");
}

void TestNonMeshAndMissingTickRetire() {
    Fixture fixture;
    pbpfps::entity_root::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&FakeSetMatrix),
                      fixture.getModelMatrixTarget);
    auto matrix = Translation(0.0F);
    fixture.vtable[18] = reinterpret_cast<void*>(0x5678);
    Require(runtime.NotifyMoved(fixture.entity.data(), matrix, 1) ==
                pbpfps::entity_root::NotifyResult::ignored,
            "non-mesh entity entered gameplay-root history");
    fixture.vtable[18] = fixture.getModelMatrixTarget;
    (void)runtime.NotifyMoved(fixture.entity.data(), matrix, 1);
    (void)runtime.CommitCompletedTick(1, 1);
    (void)runtime.CommitCompletedTick(2, 1);
    Require(runtime.BeginRender(Pair(1, 2, 0.5)).ready == 0,
            "unstaged entity survived the completed-tick boundary");
    (void)runtime.EndRender();
}

} // namespace

int main() {
    TestGameplayRootOverrideAndRestore();
    TestNonMeshAndMissingTickRetire();
    std::cout << "entity root runtime tests passed\n";
    return EXIT_SUCCESS;
}
