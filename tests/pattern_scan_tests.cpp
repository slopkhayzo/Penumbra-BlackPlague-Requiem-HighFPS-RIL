#include "pattern_scan.h"

#include <array>
#include <iostream>

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    constexpr std::array<std::uint8_t, 10> haystack = {
        0x10, 0x20, 0xAA, 0xBB, 0xCC, 0x30, 0xAA, 0xBE, 0xCC, 0x40,
    };
    constexpr std::array<std::uint8_t, 3> exact = {0xAA, 0xBB, 0xCC};
    const auto one = pbpfps::pattern::FindUnique(haystack, exact);
    if (one.count != 1 || one.offset != 2) {
        return Fail("unique exact match was not found");
    }

    constexpr std::array<std::uint8_t, 3> wildcard = {0xAA, 0x00, 0xCC};
    const auto two = pbpfps::pattern::FindUnique(haystack, wildcard, "x?x");
    if (two.count != 2) {
        return Fail("ambiguous wildcard match was not rejected");
    }

    constexpr std::array<std::uint8_t, 2> missing = {0x99, 0x88};
    if (pbpfps::pattern::FindUnique(haystack, missing).count != 0) {
        return Fail("missing pattern produced a match");
    }
    if (pbpfps::pattern::FindUnique(haystack, exact, "xx").count != 0) {
        return Fail("invalid mask length was accepted");
    }

    std::cout << "pattern_scan_tests: PASS\n";
    return 0;
}
