#include "interaction_cursor_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::size_t kActive = 0x1E4;
constexpr std::size_t kState = 0x2C8;
constexpr std::size_t kCrossHairState = 0x2F0;
constexpr std::size_t kPosition = 0x2F4;

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

void SetPlayer(void* player, const int state,
               const pbpfps::interaction_cursor::Position position) {
    Write(player, kActive, true);
    Write(player, kState, state);
    Write(player, kCrossHairState, 3);
    Write(player, kPosition, position);
}

pbpfps::clock::Sample Phase(const std::uint64_t older,
                           const std::uint64_t newer, const double alpha,
                           const std::uint64_t generation = 7) {
    pbpfps::clock::Sample result{};
    result.kind = pbpfps::clock::SampleKind::interpolated;
    result.generation = generation;
    result.olderSerial = older;
    result.newerSerial = newer;
    result.alpha = alpha;
    return result;
}

void TestApplyAndExactRestore() {
    alignas(void*) std::array<std::byte, 0x400> player{};
    pbpfps::interaction_cursor::Runtime runtime;
    const pbpfps::interaction_cursor::Position older{200.0F, 100.0F};
    const pbpfps::interaction_cursor::Position newer{600.0F, 500.0F};
    SetPlayer(player.data(), 3, older);
    Require(runtime.CaptureCompletedTick(player.data(), 1, 7) ==
                pbpfps::interaction_cursor::FrameResult::identityChanged,
            "first interaction state did not establish identity");
    SetPlayer(player.data(), 3, newer);
    Require(runtime.CaptureCompletedTick(player.data(), 2, 7) ==
                pbpfps::interaction_cursor::FrameResult::captured,
            "second cursor endpoint was not captured");
    Require(runtime.Apply(player.data(), Phase(1, 2, 0.25)) ==
                pbpfps::interaction_cursor::FrameResult::applied,
            "valid cursor pair was not applied");
    const auto interpolated =
        Read<pbpfps::interaction_cursor::Position>(player.data(), kPosition);
    Require(std::abs(interpolated.x - 300.0F) < 1.0e-5F &&
                std::abs(interpolated.y - 200.0F) < 1.0e-5F,
            "wrong cursor interpolation was applied");
    Require(runtime.Restore() ==
                pbpfps::interaction_cursor::FrameResult::restored,
            "cursor position restoration failed");
    const auto restored =
        Read<pbpfps::interaction_cursor::Position>(player.data(), kPosition);
    Require(restored.x == newer.x && restored.y == newer.y,
            "authoritative cursor position was not restored exactly");
}

void TestStateAndGenerationRebase() {
    alignas(void*) std::array<std::byte, 0x400> player{};
    pbpfps::interaction_cursor::Runtime runtime;
    SetPlayer(player.data(), 0, {400.0F, 300.0F});
    Require(runtime.CaptureCompletedTick(player.data(), 1, 2) ==
                pbpfps::interaction_cursor::FrameResult::ignoredState,
            "normal-look cursor was accepted");

    SetPlayer(player.data(), 6, {400.0F, 300.0F});
    Require(runtime.CaptureCompletedTick(player.data(), 2, 2) ==
                pbpfps::interaction_cursor::FrameResult::identityChanged,
            "grab-state entry did not rebase");
    SetPlayer(player.data(), 6, {500.0F, 300.0F});
    Require(runtime.CaptureCompletedTick(player.data(), 3, 3) ==
                pbpfps::interaction_cursor::FrameResult::identityChanged,
            "clock-generation change did not rebase");
    Require(runtime.Apply(player.data(), Phase(2, 3, 0.5, 3)) ==
                pbpfps::interaction_cursor::FrameResult::snapshotsMissing,
            "a cross-generation pair was accepted");

    Write(player.data(), kCrossHairState, 1);
    Require(runtime.CaptureCompletedTick(player.data(), 4, 3) ==
                pbpfps::interaction_cursor::FrameResult::identityChanged,
            "crosshair-state change did not rebase");
}

void TestRejectsUnexpectedAuthoritativeMutation() {
    alignas(void*) std::array<std::byte, 0x400> player{};
    pbpfps::interaction_cursor::Runtime runtime;
    SetPlayer(player.data(), 3, {100.0F, 100.0F});
    (void)runtime.CaptureCompletedTick(player.data(), 10, 4);
    SetPlayer(player.data(), 3, {200.0F, 200.0F});
    (void)runtime.CaptureCompletedTick(player.data(), 11, 4);
    SetPlayer(player.data(), 3, {210.0F, 200.0F});
    Require(runtime.Apply(player.data(), Phase(10, 11, 0.5, 4)) ==
                pbpfps::interaction_cursor::FrameResult::currentMismatch,
            "unexpected post-capture mutation was overwritten");
    const auto untouched =
        Read<pbpfps::interaction_cursor::Position>(player.data(), kPosition);
    Require(untouched.x == 210.0F && untouched.y == 200.0F,
            "rejected cursor state was modified");
}

} // namespace

int main() {
    TestApplyAndExactRestore();
    TestStateAndGenerationRebase();
    TestRejectsUnexpectedAuthoritativeMutation();
    std::cout << "interaction cursor runtime tests passed\n";
    return EXIT_SUCCESS;
}
