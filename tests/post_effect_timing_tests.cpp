#include "post_effect_timing.h"

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

void TestReferenceFrameIsUnchanged() {
    float corrected = 0.0F;
    Require(pbpfps::post_effect::CorrectImageTrailAmount(
                0.8F, 1.0F / 60.0F, 60.0F, corrected),
            "reference frame rejected");
    Require(std::abs(corrected - 0.8F) < 1.0e-6F,
            "reference frame changed the stock amount");
}

void TestRealTimeRetention() {
    constexpr double stock = 0.8;
    const double referenceOneSecond = std::pow(stock, 60.0);
    for (const float rate : {90.0F, 120.0F, 144.0F, 165.0F, 240.0F, 360.0F}) {
        float corrected = 0.0F;
        Require(pbpfps::post_effect::CorrectImageTrailAmount(
                    static_cast<float>(stock), 1.0F / rate, 60.0F, corrected),
                "valid high-rate frame rejected");
        const double observed = std::pow(static_cast<double>(corrected), rate);
        Require(std::abs(observed - referenceOneSecond) < 2.0e-7,
                "one-second retention changed with presentation rate");
    }
}

void TestBoundariesAndInvalidValues() {
    float corrected = -1.0F;
    Require(pbpfps::post_effect::CorrectImageTrailAmount(
                0.0F, 1.0F / 240.0F, 60.0F, corrected) && corrected == 0.0F,
            "zero amount failed");
    Require(pbpfps::post_effect::CorrectImageTrailAmount(
                1.0F, 1.0F / 240.0F, 60.0F, corrected) && corrected == 1.0F,
            "one amount failed");
    Require(!pbpfps::post_effect::CorrectImageTrailAmount(
                -0.1F, 1.0F / 60.0F, 60.0F, corrected),
            "negative amount accepted");
    Require(!pbpfps::post_effect::CorrectImageTrailAmount(
                1.1F, 1.0F / 60.0F, 60.0F, corrected),
            "amount above one accepted");
    Require(!pbpfps::post_effect::CorrectImageTrailAmount(
                0.8F, 0.0F, 60.0F, corrected),
            "zero frame interval accepted");
    Require(!pbpfps::post_effect::CorrectImageTrailAmount(
                std::numeric_limits<float>::quiet_NaN(), 1.0F / 60.0F,
                60.0F, corrected),
            "NaN amount accepted");
}

void TestMotionBlurReferenceAndRateScaling() {
    float corrected = 0.0F;
    Require(pbpfps::post_effect::CorrectMotionBlurScale(
                0.3F, 1.0F / 60.0F, 60.0F, corrected) &&
                std::abs(corrected - 0.3F) < 1.0e-6F,
            "reference motion-blur scale changed");
    Require(pbpfps::post_effect::CorrectMotionBlurScale(
                0.3F, 1.0F / 350.0F, 60.0F, corrected) &&
                std::abs(corrected - 1.75F) < 1.0e-5F,
            "high-rate motion-blur scale was not normalized");
    Require(pbpfps::post_effect::CorrectMotionBlurScale(
                0.3F, 1.0F / 30.0F, 60.0F, corrected) &&
                std::abs(corrected - 0.15F) < 1.0e-6F,
            "low-rate motion-blur scale was not normalized");
}

void TestMotionBlurInvalidValues() {
    float corrected = 0.0F;
    Require(!pbpfps::post_effect::CorrectMotionBlurScale(
                -0.1F, 1.0F / 60.0F, 60.0F, corrected),
            "negative motion-blur scale accepted");
    Require(!pbpfps::post_effect::CorrectMotionBlurScale(
                0.3F, 0.0F, 60.0F, corrected),
            "zero motion-blur interval accepted");
    Require(!pbpfps::post_effect::CorrectMotionBlurScale(
                8.0F, 1.0F / 1000.0F, 60.0F, corrected),
            "unsafe corrected motion-blur scale accepted");
}

} // namespace

int main() {
    TestReferenceFrameIsUnchanged();
    TestRealTimeRetention();
    TestBoundariesAndInvalidValues();
    TestMotionBlurReferenceAndRateScaling();
    TestMotionBlurInvalidValues();
    std::cout << "post_effect_timing_tests: PASS\n";
    return 0;
}
