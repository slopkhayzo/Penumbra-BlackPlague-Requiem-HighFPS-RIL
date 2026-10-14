#include "pose_interpolation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

pbpfps::transform::Matrix Translation(const float x) {
    auto matrix = pbpfps::transform::Identity();
    matrix.values[3] = x;
    return matrix;
}

pbpfps::pose::Pose MakePose(const float base,
                           const std::size_t count = 2) {
    pbpfps::pose::Pose result{};
    result.boneCount = count;
    for (std::size_t index = 0; index < count; ++index) {
        result.bones[index] =
            Translation(base + static_cast<float>(index));
    }
    return result;
}

pbpfps::clock::Sample Pair(const std::uint64_t older,
                          const std::uint64_t newer,
                          const double alpha) {
    pbpfps::clock::Sample sample{};
    sample.kind = pbpfps::clock::SampleKind::interpolated;
    sample.generation = 7;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = alpha;
    return sample;
}

void TestAtomicPairAndAffineInterpolation() {
    pbpfps::pose::History history;
    auto older = MakePose(0.0F);
    auto newer = MakePose(2.0F);
    older.bones[0].values[0] = 0.9F;
    newer.bones[0].values[0] = 1.1F;
    Require(history.Publish(4, 7, older) ==
                pbpfps::pose::PublishKind::resetGeneration,
            "first pose did not establish its generation");
    Require(history.Publish(5, 7, newer) ==
                pbpfps::pose::PublishKind::appended,
            "second pose did not append");
    const auto sampled = history.Evaluate(Pair(4, 5, 0.5));
    Require(sampled.kind == pbpfps::pose::SampleKind::applied &&
                sampled.pose.boneCount == 2 &&
                std::abs(sampled.pose.bones[0].values[0] - 1.0F) < 1.0e-5F &&
                std::abs(sampled.pose.bones[0].values[3] - 1.0F) < 1.0e-5F &&
                std::abs(sampled.pose.bones[1].values[3] - 2.0F) < 1.0e-5F,
            "affine bones were not sampled from one atomic pair");
}

void TestResetAndValidationGates() {
    pbpfps::pose::History history;
    (void)history.Publish(1, 7, MakePose(0.0F));
    Require(history.Publish(3, 7, MakePose(1.0F)) ==
                pbpfps::pose::PublishKind::resetMissingTick,
            "missing tick did not reset pose history");
    Require(history.Publish(4, 7, MakePose(1.0F, 3)) ==
                pbpfps::pose::PublishKind::resetBoneCount,
            "bone-count change did not reset pose history");
    auto invalid = MakePose(2.0F, 3);
    invalid.bones[1].values[15] = 0.0F;
    Require(history.Publish(5, 7, invalid) ==
                pbpfps::pose::PublishKind::invalid,
            "non-affine palette was accepted");
}

} // namespace

int main() {
    TestAtomicPairAndAffineInterpolation();
    TestResetAndValidationGates();
    std::cout << "pose interpolation tests passed\n";
    return EXIT_SUCCESS;
}
