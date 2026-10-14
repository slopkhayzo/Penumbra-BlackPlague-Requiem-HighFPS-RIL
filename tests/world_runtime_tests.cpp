#include "world_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::size_t kNodeLocalMatrix = 0x68;

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

void WriteMatrix(void* node, const pbpfps::transform::Matrix& matrix) {
    std::memcpy(static_cast<std::byte*>(node) + kNodeLocalMatrix, &matrix,
                sizeof(matrix));
}

pbpfps::transform::Matrix ReadMatrix(const void* node) {
    pbpfps::transform::Matrix result{};
    std::memcpy(&result,
                static_cast<const std::byte*>(node) + kNodeLocalMatrix,
                sizeof(result));
    return result;
}

void __fastcall SetMatrix(void* self, void*,
                          const pbpfps::transform::Matrix& matrix, bool) {
    WriteMatrix(self, matrix);
}

pbpfps::clock::Sample Phase(const double alpha) {
    pbpfps::clock::Sample result{};
    result.kind = pbpfps::clock::SampleKind::interpolated;
    result.generation = 3;
    result.olderSerial = 1;
    result.newerSerial = 2;
    result.alpha = alpha;
    return result;
}

void TestRigidInterpolation() {
    auto older = Translation(0.0F);
    auto newer = Translation(2.0F);
    newer.values[0] = 0.0F;
    newer.values[1] = -1.0F;
    newer.values[4] = 1.0F;
    newer.values[5] = 0.0F;
    pbpfps::transform::Matrix midpoint{};
    Require(pbpfps::transform::Interpolate(older, newer, 0.5, midpoint),
            "valid rigid transforms were rejected");
    Require(std::abs(midpoint.values[3] - 1.0F) < 1.0e-5F,
            "translation midpoint was incorrect");
    Require(pbpfps::transform::IsRigid(midpoint),
            "rotation interpolation lost orthonormality");
}

void TestApplyRestoreAndMissingPair() {
    alignas(void*) std::array<std::byte, 0x200> node{};
    pbpfps::world::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix));

    const auto older = Translation(0.0F);
    const auto newer = Translation(2.0F);
    WriteMatrix(node.data(), older);
    Require(runtime.NotifyNodeMoved(node.data(), older, 1) ==
                pbpfps::world::NotifyResult::staged,
            "first physics endpoint was not staged");
    Require(runtime.CommitCompletedTick(1, 3).reset == 1,
            "first endpoint did not establish generation");
    WriteMatrix(node.data(), newer);
    (void)runtime.NotifyNodeMoved(node.data(), newer, 2);
    Require(runtime.CommitCompletedTick(2, 3).captured == 1,
            "second physics endpoint was not captured");

    const auto begun = runtime.BeginRender(Phase(0.25));
    Require(begun.ready == 1 && begun.applyFailed == 0,
            "world root was not render-ready");
    Require(std::abs(ReadMatrix(node.data()).values[3] - 0.5F) < 1.0e-5F,
            "interpolated root was not applied");
    Require(runtime.EndRender().restoreFailed == 0,
            "world root restoration failed");
    Require(ReadMatrix(node.data()).values[3] == 2.0F,
            "authoritative root was not restored exactly");

    const auto third = Translation(2.5F);
    WriteMatrix(node.data(), third);
    (void)runtime.NotifyNodeMoved(node.data(), third, 4);
    Require(runtime.CommitCompletedTick(4, 3).reset == 1,
            "missing physics tick did not reset history");
}

void TestExternalMutationFailsClosed() {
    alignas(void*) std::array<std::byte, 0x200> node{};
    pbpfps::world::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix));
    const auto first = Translation(0.0F);
    const auto second = Translation(1.0F);
    WriteMatrix(node.data(), first);
    (void)runtime.NotifyNodeMoved(node.data(), first, 1);
    (void)runtime.CommitCompletedTick(1, 3);
    WriteMatrix(node.data(), second);
    (void)runtime.NotifyNodeMoved(node.data(), second, 2);
    (void)runtime.CommitCompletedTick(2, 3);
    WriteMatrix(node.data(), Translation(9.0F));
    const auto result = runtime.BeginRender(Phase(0.5));
    Require(result.invalid == 1 && result.ready == 0,
            "external root mutation was overwritten");
    (void)runtime.EndRender();
    Require(ReadMatrix(node.data()).values[3] == 9.0F,
            "fail-closed path changed external state");
}

void TestFullTableKeepsNodesFromThePendingTick() {
    using pbpfps::world::NotifyResult;
    using pbpfps::world::Runtime;
    static std::array<std::array<std::byte, 0x200>, Runtime::kMaximumNodes + 1>
        nodes{};
    static Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix));
    const auto matrix = Translation(0.0F);
    for (std::size_t index = 0; index < Runtime::kMaximumNodes; ++index) {
        Require(runtime.NotifyNodeMoved(nodes[index].data(), matrix, 1) ==
                    NotifyResult::staged,
                "node was rejected before the table filled");
    }
    void* const overflow = nodes[Runtime::kMaximumNodes].data();
    Require(runtime.NotifyNodeMoved(overflow, matrix, 1) ==
                NotifyResult::capacityExhausted,
            "a node from the pending tick was evicted by a full table");
    const auto committed = runtime.CommitCompletedTick(1, 3);
    Require(committed.captured + committed.reset == Runtime::kMaximumNodes &&
                committed.invalid == 0,
            "nodes from the pending tick lost their staged transform");
    Require(runtime.NotifyNodeMoved(overflow, matrix, 2) ==
                NotifyResult::staged,
            "a node idle since an earlier tick was not recycled");
}

// The engine destroys a node without notice (a picked-up item takes its body
// node with it). The freed block still holds the last matrix, so only the
// rewritten first word shows that the object is gone.
void TestDestroyedNodeIsNeverWritten() {
    alignas(void*) std::array<std::byte, 0x200> node{};
    const std::uintptr_t liveVtable = 0x00682318;
    std::memcpy(node.data(), &liveVtable, sizeof(liveVtable));
    static pbpfps::world::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix));
    WriteMatrix(node.data(), Translation(0.0F));
    (void)runtime.NotifyNodeMoved(node.data(), Translation(0.0F), 1);
    (void)runtime.CommitCompletedTick(1, 3);
    WriteMatrix(node.data(), Translation(2.0F));
    (void)runtime.NotifyNodeMoved(node.data(), Translation(2.0F), 2);
    (void)runtime.CommitCompletedTick(2, 3);

    // Destroyed: first word replaced, matrix bytes left as they were.
    const std::uintptr_t freedWord = 0x18FB0000;
    std::memcpy(node.data(), &freedWord, sizeof(freedWord));
    const auto result = runtime.BeginRender(Phase(0.5));
    Require(result.invalid == 1 && result.ready == 0,
            "a destroyed node was presented");
    Require(std::abs(ReadMatrix(node.data()).values[3] - 2.0F) < 1.0e-6F,
            "a destroyed node was written through SetMatrix");
    Require(runtime.EndRender().restoreFailed == 0,
            "a destroyed node left a pending restoration");
    // The slot is dropped, so later frames do not touch it either.
    const auto again = runtime.BeginRender(Phase(0.5));
    Require(again.invalid == 0 && again.ready == 0,
            "a destroyed node stayed in the table");
    (void)runtime.EndRender();
}

void TestUnmappedNodeFailsClosed() {
    static pbpfps::world::Runtime runtime;
    runtime.Configure(reinterpret_cast<void*>(&SetMatrix));
    // The first pages are never mapped; reads must fault into a clean reject.
    void* const stale = reinterpret_cast<void*>(std::uintptr_t{0x1000});
    (void)runtime.NotifyNodeMoved(stale, Translation(0.0F), 1);
    (void)runtime.CommitCompletedTick(1, 3);
    (void)runtime.NotifyNodeMoved(stale, Translation(1.0F), 2);
    (void)runtime.CommitCompletedTick(2, 3);
    const auto result = runtime.BeginRender(Phase(0.5));
    Require(result.invalid == 1 && result.ready == 0,
            "unmapped node was not rejected");
    Require(runtime.EndRender().restoreFailed == 0,
            "rejected node left a pending restoration");
}

} // namespace

int main() {
    TestRigidInterpolation();
    TestApplyRestoreAndMissingPair();
    TestExternalMutationFailsClosed();
    TestFullTableKeepsNodesFromThePendingTick();
    TestDestroyedNodeIsNeverWritten();
    TestUnmappedNodeFailsClosed();
    std::cout << "world runtime tests passed\n";
    return EXIT_SUCCESS;
}
