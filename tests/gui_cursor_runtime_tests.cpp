#include "gui_cursor_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::size_t kPlayerPosition = 0x2F4;

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

void TestInventoryPreviewAndExactRestore() {
    alignas(void*) std::array<std::byte, 0x200> inventory{};
    alignas(void*) std::array<std::byte, 0x400> player{};
    const pbpfps::gui_cursor::Position fixed{400.0F, 300.0F};
    Write(inventory.data(), 0xAC, fixed);
    Write(player.data(), kPlayerPosition, fixed);

    pbpfps::gui_cursor::Runtime runtime;
    Require(runtime.Apply(inventory.data(),
                          pbpfps::gui_cursor::OwnerKind::inventory,
                          player.data(), {24, -18}, 1.5F, 1920.0F,
                          1080.0F) ==
                pbpfps::gui_cursor::FrameResult::applied,
            "inventory preview was not applied");
    const auto ownerPresented =
        Read<pbpfps::gui_cursor::Position>(inventory.data(), 0xAC);
    const auto playerPresented =
        Read<pbpfps::gui_cursor::Position>(player.data(), kPlayerPosition);
    Require(std::abs(ownerPresented.x - 415.0F) < 1.0e-6F &&
                std::abs(ownerPresented.y - 285.0F) < 1.0e-6F &&
                ownerPresented.x == playerPresented.x &&
                ownerPresented.y == playerPresented.y,
            "inventory and player preview positions diverged");
    Require(runtime.Restore() ==
                pbpfps::gui_cursor::FrameResult::restored,
            "inventory preview restoration failed");
    const auto ownerRestored =
        Read<pbpfps::gui_cursor::Position>(inventory.data(), 0xAC);
    const auto playerRestored =
        Read<pbpfps::gui_cursor::Position>(player.data(), kPlayerPosition);
    Require(ownerRestored.x == fixed.x && ownerRestored.y == fixed.y &&
                playerRestored.x == fixed.x && playerRestored.y == fixed.y,
            "authoritative inventory positions were not restored exactly");
}

void TestOwnerLayoutsAndClamp() {
    struct Case {
        pbpfps::gui_cursor::OwnerKind kind;
        std::size_t offset;
        bool mirrors;
    };
    const std::array cases{
        Case{pbpfps::gui_cursor::OwnerKind::mainMenu, 0xD4, false},
        Case{pbpfps::gui_cursor::OwnerKind::deathMenu, 0x38, true},
        Case{pbpfps::gui_cursor::OwnerKind::numericalPanel, 0x38, true},
        Case{pbpfps::gui_cursor::OwnerKind::notebook, 0x84, true},
        Case{pbpfps::gui_cursor::OwnerKind::inventory, 0xAC, true},
    };
    for (const Case& test : cases) {
        alignas(void*) std::array<std::byte, 0x200> owner{};
        alignas(void*) std::array<std::byte, 0x400> player{};
        Write(owner.data(), test.offset,
              pbpfps::gui_cursor::Position{799.0F, 1.0F});
        Write(player.data(), kPlayerPosition,
              pbpfps::gui_cursor::Position{799.0F, 1.0F});
        pbpfps::gui_cursor::Runtime runtime;
        Require(runtime.Apply(owner.data(), test.kind,
                              test.mirrors ? player.data() : nullptr,
                              {100, -100}, 1.0F, 800.0F, 600.0F) ==
                    pbpfps::gui_cursor::FrameResult::applied,
                "owner layout preview was rejected");
        const auto presented =
            Read<pbpfps::gui_cursor::Position>(owner.data(), test.offset);
        Require(presented.x == 800.0F && presented.y == 0.0F,
                "owner layout did not use the retail clamp");
        Require(runtime.Restore() ==
                    pbpfps::gui_cursor::FrameResult::restored,
                "owner layout did not restore");
    }
}

void TestInvalidAndStationaryFailClosed() {
    alignas(void*) std::array<std::byte, 0x200> owner{};
    alignas(void*) std::array<std::byte, 0x400> player{};
    Write(owner.data(), 0xAC, pbpfps::gui_cursor::Position{400.0F, 300.0F});
    Write(player.data(), kPlayerPosition,
          pbpfps::gui_cursor::Position{400.0F, 300.0F});
    pbpfps::gui_cursor::Runtime runtime;
    Require(runtime.Apply(owner.data(),
                          pbpfps::gui_cursor::OwnerKind::inventory,
                          player.data(), {}, 1.0F, 1920.0F, 1080.0F) ==
                pbpfps::gui_cursor::FrameResult::stationary,
            "zero GUI movement was not skipped");
    Require(runtime.Apply(owner.data(),
                          pbpfps::gui_cursor::OwnerKind::inventory,
                          nullptr, {1, 1}, 1.0F, 1920.0F, 1080.0F) ==
                pbpfps::gui_cursor::FrameResult::invalidInput,
            "mirrored GUI owner accepted a null player");
}

} // namespace

int main() {
    TestInventoryPreviewAndExactRestore();
    TestOwnerLayoutsAndClamp();
    TestInvalidAndStationaryFailClosed();
    std::cout << "GUI cursor runtime tests passed\n";
    return EXIT_SUCCESS;
}
