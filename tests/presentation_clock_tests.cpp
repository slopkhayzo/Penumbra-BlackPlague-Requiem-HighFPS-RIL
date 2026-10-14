#include "presentation_clock.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void TestAdjacentPair() {
    pbpfps::clock::PresentationClock clock(1.0 / 60.0);
    Require(clock.Publish(10, 1.0).accepted, "first snapshot rejected");
    Require(clock.Publish(11, 1.0 + 1.0 / 60.0).accepted,
            "second snapshot rejected");
    const auto sample = clock.Evaluate(1.02, 0.25);
    Require(sample.kind == pbpfps::clock::SampleKind::interpolated,
            "adjacent pair did not interpolate");
    Require(sample.olderSerial == 10 && sample.newerSerial == 11,
            "wrong serial pair");
    Require(std::abs(sample.alpha - 0.25) < 1.0e-12, "wrong alpha");
    Require(std::abs(sample.targetTimeSeconds - (10.25 / 60.0)) < 1.0e-12,
            "wrong target time");
}

void TestRateParameterization() {
    pbpfps::clock::PresentationClock clock(1.0 / 120.0);
    Require(clock.Publish(2, 0.0).accepted, "120 Hz first snapshot rejected");
    Require(clock.Publish(3, 1.0 / 120.0).accepted,
            "120 Hz second snapshot rejected");
    const auto sample = clock.Evaluate(0.01, 0.5);
    Require(std::abs(sample.targetTimeSeconds - (2.5 / 120.0)) < 1.0e-12,
            "native step was not parameterized");
}

void TestDiscontinuities() {
    pbpfps::clock::PresentationClock clock;
    Require(clock.Publish(1, 0.0).accepted,
            "discontinuity setup snapshot rejected");
    const auto skipped = clock.Publish(3, 0.02);
    Require(skipped.accepted &&
                skipped.resetReason == pbpfps::clock::ResetReason::missingSnapshot,
            "missing snapshot did not reset");
    Require(clock.SnapshotCount() == 1, "reset did not retain only new endpoint");

    const auto invalid = clock.Publish(
        4, std::numeric_limits<double>::quiet_NaN());
    Require(!invalid.accepted &&
                invalid.resetReason ==
                    pbpfps::clock::ResetReason::nonMonotonicWallTime,
            "non-finite wall time was accepted");
}

void TestLogicTimerAlpha() {
    double alpha = 0.0;
    double raw = 0.0;
    Require(pbpfps::clock::LogicTimerAlpha(1000.0, 1000.0 / 60.0, 1.0,
                                          1000.0 - 0.5 * (1000.0 / 60.0),
                                          0.0, alpha, &raw),
            "valid timer phase rejected");
    Require(std::abs(alpha - 0.5) < 1.0e-12 &&
                std::abs(raw - 0.5) < 1.0e-12,
            "timer phase was computed incorrectly");
    Require(!pbpfps::clock::LogicTimerAlpha(0.0, 0.0, 1.0, 0.0, 0.0,
                                           alpha, nullptr),
            "zero step was accepted");
}

} // namespace

int main() {
    TestAdjacentPair();
    TestRateParameterization();
    TestDiscontinuities();
    TestLogicTimerAlpha();
    std::cout << "presentation_clock_tests: PASS\n";
    return 0;
}
