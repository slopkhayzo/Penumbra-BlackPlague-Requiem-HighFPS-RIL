#include "motion_blur_runtime.h"

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
void Write(std::array<std::uint8_t, 0x90>& renderer,
           const std::size_t offset, const T& value) {
    std::memcpy(renderer.data() + offset, &value, sizeof(value));
}

template <typename T>
T Read(const std::array<std::uint8_t, 0x90>& renderer,
       const std::size_t offset) {
    T value{};
    std::memcpy(&value, renderer.data() + offset, sizeof(value));
    return value;
}

void MakeAvailable(std::array<std::uint8_t, 0x90>& renderer) {
    Write(renderer, 0x50, reinterpret_cast<void*>(0x1000));
    Write(renderer, 0x54, reinterpret_cast<void*>(0x2000));
    Write(renderer, 0x7C, std::uint8_t{1});
    Write(renderer, 0x80, 0.3F);
}

void TestInactiveUnavailableAndFirstPassRemainUntouched() {
    std::array<std::uint8_t, 0x90> renderer{};
    pbpfps::motion_blur::Runtime runtime;
    Write(renderer, 0x80, 0.3F);
    auto result = runtime.Begin(renderer.data(), 1.0F / 350.0F);
    Require(result.kind == pbpfps::motion_blur::BeginKind::inactive,
            "inactive motion blur was modified");

    Write(renderer, 0x7C, std::uint8_t{1});
    result = runtime.Begin(renderer.data(), 1.0F / 350.0F);
    Require(result.kind == pbpfps::motion_blur::BeginKind::unavailable,
            "missing shader programs were accepted");

    MakeAvailable(renderer);
    Write(renderer, 0x84, std::uint8_t{1});
    result = runtime.Begin(renderer.data(), 1.0F / 350.0F);
    Require(result.kind == pbpfps::motion_blur::BeginKind::firstPass &&
                Read<float>(renderer, 0x80) == 0.3F,
            "first motion-blur pass changed its scale");
}

void TestCorrectionAndExactRestore() {
    std::array<std::uint8_t, 0x90> renderer{};
    MakeAvailable(renderer);
    pbpfps::motion_blur::Runtime runtime;
    const auto result = runtime.Begin(renderer.data(), 1.0F / 350.0F);
    Require(result.kind == pbpfps::motion_blur::BeginKind::applied &&
                std::abs(result.correctedScale - 1.75F) < 1.0e-5F,
            "motion-blur scale was not corrected");
    Require(std::abs(Read<float>(renderer, 0x80) - 1.75F) < 1.0e-5F,
            "corrected motion-blur scale was not installed");
    Require(runtime.Begin(renderer.data(), 1.0F / 350.0F).kind ==
                pbpfps::motion_blur::BeginKind::reentrant,
            "nested motion-blur override was not rejected");
    Require(runtime.End() == pbpfps::motion_blur::EndKind::restored &&
                Read<float>(renderer, 0x80) == 0.3F,
            "motion-blur scale was not restored exactly");
}

void TestInvalidIntervalFailsClosed() {
    std::array<std::uint8_t, 0x90> renderer{};
    MakeAvailable(renderer);
    pbpfps::motion_blur::Runtime runtime;
    Require(runtime.Begin(renderer.data(), 0.0F).kind ==
                pbpfps::motion_blur::BeginKind::invalid &&
                Read<float>(renderer, 0x80) == 0.3F,
            "invalid interval changed motion-blur state");
}

} // namespace

int main() {
    TestInactiveUnavailableAndFirstPassRemainUntouched();
    TestCorrectionAndExactRestore();
    TestInvalidIntervalFailsClosed();
    std::cout << "motion blur runtime tests passed\n";
    return EXIT_SUCCESS;
}
