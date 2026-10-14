#include "guarded_memory.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void TestValidAccessRoundTrips() {
    std::array<std::uint32_t, 4> storage{1, 2, 3, 4};
    std::uint32_t value = 0;
    Require(pbpfps::memory::TryReadAt(storage.data(), 8, value) && value == 3,
            "valid read failed");
    Require(pbpfps::memory::TryWriteAt(storage.data(), 4, std::uint32_t{9}) &&
                storage[1] == 9,
            "valid write failed");
}

void TestNullAndUnmappedAddressesAreRejected() {
    std::uint32_t value = 7;
    Require(!pbpfps::memory::TryReadAt(nullptr, 0, value),
            "null base was read");
    Require(!pbpfps::memory::TryRead(
                reinterpret_cast<const void*>(std::uintptr_t{0x1000}), value),
            "unmapped address was read");
    Require(!pbpfps::memory::TryWrite(
                reinterpret_cast<void*>(std::uintptr_t{0x1000}), value),
            "unmapped address was written");
}

void TestProtectionChangesAreRespected() {
    void* const page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    Require(page != nullptr, "test page allocation failed");
    Require(pbpfps::memory::TryWrite(page, std::uint32_t{5}),
            "writable page rejected a write");

    DWORD previous = 0;
    Require(VirtualProtect(page, 4096, PAGE_READONLY, &previous) != FALSE,
            "test page protection change failed");
    std::uint32_t value = 0;
    Require(pbpfps::memory::TryRead(page, value) && value == 5,
            "read-only page rejected a read");
    Require(!pbpfps::memory::TryWrite(page, std::uint32_t{6}),
            "read-only page accepted a write");

    // A freed object: the address was valid a moment ago.
    Require(VirtualFree(page, 0, MEM_RELEASE) != FALSE,
            "test page release failed");
    Require(!pbpfps::memory::TryRead(page, value),
            "released page was read");
}

void TestVtableEntryMatching() {
    std::array<const void*, 4> vtable{};
    const int target = 0;
    vtable[2] = &target;
    struct Object {
        const void* const* vtable;
    } object{vtable.data()};
    Require(pbpfps::memory::HasVtableEntry(&object, 2, &target),
            "matching vtable entry was rejected");
    Require(!pbpfps::memory::HasVtableEntry(&object, 1, &target),
            "mismatched vtable entry was accepted");
    Require(!pbpfps::memory::HasVtableEntry(&object, 1, nullptr),
            "null expectation matched an empty vtable entry");
    object.vtable =
        reinterpret_cast<const void* const*>(std::uintptr_t{0x1000});
    Require(!pbpfps::memory::HasVtableEntry(&object, 2, &target),
            "unmapped vtable was accepted");
}

} // namespace

int main() {
    TestValidAccessRoundTrips();
    TestNullAndUnmappedAddressesAreRejected();
    TestProtectionChangesAreRespected();
    TestVtableEntryMatching();
    std::cout << "guarded memory tests passed\n";
    return EXIT_SUCCESS;
}
