#include "image_trail_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

void Require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename T>
void Write(std::array<std::uint8_t, 0x40>& renderer,
           const std::size_t offset, const T& value) {
    std::memcpy(renderer.data() + offset, &value, sizeof(value));
}

template <typename T>
T Read(const std::array<std::uint8_t, 0x40>& renderer,
       const std::size_t offset) {
    T value{};
    std::memcpy(&value, renderer.data() + offset, sizeof(value));
    return value;
}

void TestInactiveAndFirstPassRemainUntouched() {
    std::array<std::uint8_t, 0x40> renderer{};
    pbpfps::image_trail::Runtime runtime;
    Write(renderer, 0x30, 0.8F);

    auto result = runtime.Begin(renderer.data(), 1.0F / 350.0F);
    Require(result.kind == pbpfps::image_trail::BeginKind::inactive,
            "inactive image trail was modified");
    Require(runtime.End() == pbpfps::image_trail::EndKind::none,
            "inactive image trail created an override");

    Write(renderer, 0x28, static_cast<std::uint8_t>(1));
    Write(renderer, 0x29, static_cast<std::uint8_t>(1));
    result = runtime.Begin(renderer.data(), 1.0F / 350.0F);
    Require(result.kind == pbpfps::image_trail::BeginKind::firstPass,
            "first pass did not bypass correction");
    Require(Read<float>(renderer, 0x30) == 0.8F,
            "first-pass amount changed");
}

void TestCorrectionAndExactRestoration() {
    std::array<std::uint8_t, 0x40> renderer{};
    pbpfps::image_trail::Runtime runtime;
    Write(renderer, 0x28, static_cast<std::uint8_t>(1));
    Write(renderer, 0x29, static_cast<std::uint8_t>(0));
    Write(renderer, 0x30, 0.8F);

    const auto result = runtime.Begin(renderer.data(), 1.0F / 350.0F);
    Require(result.kind == pbpfps::image_trail::BeginKind::applied,
            "valid image trail was not corrected");
    Require(result.correctedAmount > result.stockAmount &&
                result.correctedAmount < 1.0F,
            "high-rate retention was not increased correctly");
    Require(std::abs(Read<float>(renderer, 0x30) -
                     result.correctedAmount) < 1.0e-7F,
            "corrected amount was not installed");
    Require(runtime.Begin(renderer.data(), 1.0F / 350.0F).kind ==
                pbpfps::image_trail::BeginKind::reentrant,
            "nested override was not rejected");
    Require(runtime.End() == pbpfps::image_trail::EndKind::restored,
            "stock amount was not restored");
    Require(Read<float>(renderer, 0x30) == 0.8F,
            "restoration was not exact");
}

void TestInvalidStateFailsClosed() {
    std::array<std::uint8_t, 0x40> renderer{};
    pbpfps::image_trail::Runtime runtime;
    Write(renderer, 0x28, static_cast<std::uint8_t>(1));
    Write(renderer, 0x30, 1.1F);
    Require(runtime.Begin(renderer.data(), 1.0F / 350.0F).kind ==
                pbpfps::image_trail::BeginKind::invalid,
            "out-of-range amount was accepted");
    Write(renderer, 0x30, 0.8F);
    Require(runtime.Begin(renderer.data(), 0.0F).kind ==
                pbpfps::image_trail::BeginKind::invalid,
            "zero frame interval was accepted");
    Require(Read<float>(renderer, 0x30) == 0.8F,
            "invalid input changed the stock amount");
}

} // namespace

int main() {
    TestInactiveAndFirstPassRemainUntouched();
    TestCorrectionAndExactRestoration();
    TestInvalidStateFailsClosed();
    std::cout << "image trail runtime tests passed\n";
    return EXIT_SUCCESS;
}
