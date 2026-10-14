#include "camera_interpolation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

constexpr float kPi = 3.14159265358979323846F;

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

pbpfps::camera::Pose MakePose(const float x, const float pitch = 0.0F,
                             const float yaw = 0.0F,
                             const float roll = 0.0F) {
    return {{x, x * 2.0F, -x}, pitch, yaw, roll};
}

pbpfps::clock::Sample Pair(const std::uint64_t generation,
                          const std::uint64_t older,
                          const std::uint64_t newer, const double alpha) {
    pbpfps::clock::Sample sample{};
    sample.kind = pbpfps::clock::SampleKind::interpolated;
    sample.generation = generation;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = alpha;
    return sample;
}

void TestLinearPose() {
    const auto older = MakePose(0.0F, -0.2F, 0.3F, -0.1F);
    const auto newer = MakePose(2.0F, 0.2F, 0.7F, 0.1F);
    pbpfps::camera::Pose midpoint{};
    Require(pbpfps::camera::Interpolate(older, newer, 0.5, midpoint),
            "valid midpoint was rejected");
    Require(std::abs(midpoint.position.x - 1.0F) < 1.0e-5F &&
                std::abs(midpoint.position.y - 2.0F) < 1.0e-5F &&
                std::abs(midpoint.position.z + 1.0F) < 1.0e-5F,
            "position was not interpolated linearly");
    Require(std::abs(midpoint.pitch) < 1.0e-5F &&
                std::abs(midpoint.yaw - 0.5F) < 1.0e-5F &&
                std::abs(midpoint.roll) < 1.0e-5F,
            "orientation was not interpolated");
}

void TestShortestArcAndRebase() {
    const auto older = MakePose(0.0F, 0.0F, 179.0F * kPi / 180.0F);
    const auto newer = MakePose(0.0F, 0.0F, -179.0F * kPi / 180.0F);
    pbpfps::camera::Pose midpoint{};
    Require(pbpfps::camera::Interpolate(older, newer, 0.5, midpoint),
            "yaw-wrap interpolation was rejected");
    Require(std::abs(std::abs(midpoint.yaw) - kPi) < 1.0e-4F,
            "yaw wrap took the long arc");
    const float rebased = pbpfps::camera::RebaseAngleNear(midpoint.yaw, newer.yaw);
    Require(std::abs(rebased - newer.yaw) < 0.05F,
            "equivalent angle was not rebased near authoritative yaw");
}

void TestHistoryPairingAndResets() {
    pbpfps::camera::History history;
    Require(history.Publish(10, 4, MakePose(0.0F)).kind ==
                pbpfps::camera::PublishKind::resetGeneration,
            "first pose did not establish generation");
    Require(history.Publish(11, 4, MakePose(1.0F)).kind ==
                pbpfps::camera::PublishKind::appended,
            "second pose was not appended");
    const auto sampled = history.Evaluate(Pair(4, 10, 11, 0.25));
    Require(sampled.kind == pbpfps::camera::SampleKind::applied &&
                std::abs(sampled.pose.position.x - 0.25F) < 1.0e-5F,
            "history did not sample the exact serial pair");
    Require(history.Publish(13, 4, MakePose(1.1F)).kind ==
                pbpfps::camera::PublishKind::resetMissingTick,
            "missing fixed tick did not reset history");
    Require(history.Publish(14, 4, MakePose(20.0F)).kind ==
                pbpfps::camera::PublishKind::resetDiscontinuity,
            "teleport was interpolated");
}

void TestInvalidInputFailsClosed() {
    pbpfps::camera::Pose invalid = MakePose(0.0F);
    invalid.yaw = std::nanf("");
    pbpfps::camera::Pose output{};
    Require(!pbpfps::camera::Interpolate(invalid, MakePose(1.0F), 0.5,
                                        output),
            "non-finite orientation was accepted");
    Require(!pbpfps::camera::Interpolate(MakePose(0.0F), MakePose(1.0F),
                                        1.1, output),
            "out-of-range alpha was accepted");
}

} // namespace

int main() {
    TestLinearPose();
    TestShortestArcAndRebase();
    TestHistoryPairingAndResets();
    TestInvalidInputFailsClosed();
    std::cout << "camera interpolation tests passed\n";
    return EXIT_SUCCESS;
}
