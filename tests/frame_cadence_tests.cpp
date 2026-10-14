#include "frame_cadence.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    pbpfps::cadence::FrameCadence cadence;
    for (unsigned int index = 0; index < 100; ++index) {
        cadence.RecordIntervalMicroseconds(index < 99 ? 2'778 : 4'000);
    }
    cadence.RecordIntervalMicroseconds(120'000);
    cadence.RecordIntervalMicroseconds(300'000);
    cadence.RecordIntervalMicroseconds(0);

    const auto snapshot = cadence.TakeSnapshot();
    Require(snapshot.samples == 101, "ordinary and overflow samples not retained");
    Require(snapshot.p50Milliseconds > 2.77 && snapshot.p50Milliseconds < 2.79,
            "360 Hz median outside its 10 us bin");
    Require(snapshot.p95Milliseconds > 2.77 && snapshot.p95Milliseconds < 2.79,
            "360 Hz p95 outside its 10 us bin");
    Require(snapshot.maxMilliseconds > 99.99 && snapshot.maxMilliseconds < 100.01,
            "overflow sample did not clamp to final bin");
    Require(snapshot.overflow == 1, "histogram overflow not counted");
    Require(snapshot.longGaps == 1, "long gap not counted");
    Require(snapshot.invalid == 1, "invalid interval not counted");

    const auto cleared = cadence.TakeSnapshot();
    Require(cleared.samples == 0 && cleared.longGaps == 0 &&
                cleared.overflow == 0 && cleared.invalid == 0,
            "snapshot did not retire counters");

    pbpfps::cadence::FrameCadence qpc;
    qpc.RecordQpc(1'000, 1'000);
    qpc.RecordQpc(1'003, 1'000);
    const auto qpcSnapshot = qpc.TakeSnapshot();
    Require(qpcSnapshot.samples == 1 &&
                std::fabs(qpcSnapshot.meanMilliseconds - 3.005) < 0.011,
            "QPC conversion was incorrect");

    std::cout << "frame_cadence_tests: PASS\n";
    return 0;
}
