#include "mouse_preview_math.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

pbpfps::clock::Sample Phase(const std::uint64_t older,
                           const std::uint64_t newer, const double alpha,
                           const std::uint64_t generation = 9) {
    pbpfps::clock::Sample result{};
    result.kind = pbpfps::clock::SampleKind::interpolated;
    result.generation = generation;
    result.olderSerial = older;
    result.newerSerial = newer;
    result.alpha = alpha;
    return result;
}

void TestConservationArithmetic() {
    pbpfps::mouse::RawCounts pending{4, -7};
    Require(pbpfps::mouse::Accumulate(pending, -9, 12),
            "valid presentation counts were rejected");
    Require(pending.x == -5 && pending.y == 5,
            "presentation counts were accumulated incorrectly");

    std::int32_t deliveredX = 0;
    std::int32_t deliveredY = 0;
    Require(pbpfps::mouse::CombineForDelivery(pending, 3, -8, deliveredX,
                                              deliveredY),
            "valid pending and fresh counts could not be combined");
    Require(deliveredX == -2 && deliveredY == -3,
            "combined delivery did not conserve counts");

    pbpfps::mouse::RawCounts atLimit{1LL << 30, 0};
    Require(!pbpfps::mouse::Accumulate(atLimit, 1, 0) &&
                atLimit.x == (1LL << 30),
            "pending-count overflow did not fail without mutation");

    const pbpfps::mouse::RawCounts tooLarge{
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()), 0};
    Require(!pbpfps::mouse::CombineForDelivery(tooLarge, 1, 0, deliveredX,
                                               deliveredY) &&
                deliveredX == 1,
            "delivery overflow did not preserve the fresh stock sample");
}

void TestNormalLookScale() {
    pbpfps::mouse::AngularDelta angles{};
    Require(pbpfps::mouse::NormalLookAngles({12, -6}, 1.5F, 2.0F,
                                            1920.0F, 1080.0F, false, false,
                                            angles),
            "valid normal-look conversion failed");
    Require(std::abs(angles.yaw - -0.01875F) < 1.0e-7F &&
                std::abs(angles.pitch - 0.016666667F) < 1.0e-7F,
            "normal-look conversion did not match the retail scale");

    pbpfps::mouse::AngularDelta inverted{};
    Require(pbpfps::mouse::NormalLookAngles({12, -6}, 1.5F, 2.0F,
                                            1920.0F, 1080.0F, true, false,
                                            inverted) &&
                inverted.yaw == angles.yaw &&
                std::abs(inverted.pitch + angles.pitch) < 1.0e-7F,
            "vertical inversion changed the wrong axis");

    Require(!pbpfps::mouse::NormalLookAngles(
                {1, 1}, 1.0F, 1.0F, 0.0F, 1080.0F, false, false, angles),
            "invalid screen dimensions did not fail closed");

    pbpfps::mouse::AngularDelta equalized{};
    Require(pbpfps::mouse::NormalLookAngles(
                {12, -6}, 1.5F, 2.0F, 1920.0F, 1080.0F, false, true,
                equalized),
            "axis-equalized normal-look conversion failed");
    Require(std::abs(equalized.yaw - -0.033333335F) < 1.0e-7F &&
                std::abs(equalized.pitch - 0.016666667F) < 1.0e-7F,
            "axis equalization did not preserve Y and match X degrees/count");

    float aspectScale = 0.0F;
    Require(pbpfps::mouse::AxisEqualizationScale(1920.0F, 1080.0F,
                                                aspectScale) &&
                std::abs(aspectScale - 1.7777778F) < 1.0e-7F,
            "16:9 axis-equalization scale was incorrect");
    Require(!pbpfps::mouse::AxisEqualizationScale(1920.0F, 0.0F,
                                                 aspectScale) &&
                aspectScale == 1.0F,
            "invalid axis-equalization dimensions did not fail closed");
}

void TestDeliveryBridge() {
    const pbpfps::mouse::AngularDelta delivered{0.08F, -0.12F};
    pbpfps::mouse::AngularDelta residual{};
    Require(pbpfps::mouse::ResidualForSample(delivered, 42, 9,
                                             Phase(41, 42, 0.25), residual),
            "matching delivery bridge was rejected");
    Require(std::abs(residual.pitch - 0.06F) < 1.0e-7F &&
                std::abs(residual.yaw - -0.09F) < 1.0e-7F,
            "delivery bridge did not retain one minus alpha");

    Require(pbpfps::mouse::ResidualForSample(delivered, 42, 9,
                                             Phase(41, 42, 1.0), residual) &&
                residual.pitch == 0.0F && residual.yaw == 0.0F,
            "fully represented delivery left a residual overlay");
    Require(!pbpfps::mouse::ResidualForSample(delivered, 42, 9,
                                              Phase(42, 43, 0.25), residual),
            "a delivery leaked into the next interpolation span");
    Require(!pbpfps::mouse::ResidualForSample(delivered, 42, 9,
                                              Phase(41, 42, 0.25, 10), residual),
            "a delivery crossed a clock generation boundary");
}

void TestGuiCursorScale() {
    pbpfps::mouse::CursorDelta delta{};
    Require(pbpfps::mouse::GuiCursorDelta({24, -18}, 1.5F, 1920.0F,
                                          1080.0F, delta),
            "valid GUI cursor conversion failed");
    Require(std::abs(delta.x - 15.0F) < 1.0e-6F &&
                std::abs(delta.y - -15.0F) < 1.0e-6F,
            "GUI cursor conversion did not match the retail virtual scale");
    Require(!pbpfps::mouse::GuiCursorDelta({1, 1}, 1.0F, 0.0F, 1080.0F,
                                           delta),
            "invalid GUI screen dimensions did not fail closed");
    Require(!pbpfps::mouse::GuiCursorDelta({1, 1}, -1.0F, 1920.0F, 1080.0F,
                                           delta),
            "negative GUI sensitivity did not fail closed");
}

} // namespace

int main() {
    TestConservationArithmetic();
    TestNormalLookScale();
    TestDeliveryBridge();
    TestGuiCursorScale();
    std::cout << "mouse preview math tests passed\n";
    return EXIT_SUCCESS;
}
