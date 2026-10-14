#include "config.h"

#include <Windows.h>

#include <fstream>
#include <iostream>

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    constexpr wchar_t kRelativePath[] = L"config-tests.ini";
    wchar_t absolutePath[MAX_PATH]{};
    if (GetFullPathNameW(kRelativePath, MAX_PATH, absolutePath, nullptr) == 0) {
        return Fail("could not resolve test INI path");
    }
    {
        std::ofstream file("config-tests.ini", std::ios::binary | std::ios::trunc);
        file << "[General]\r\n"
                "Enabled=true\r\n"
                "Diagnostics=YES\r\n"
                "[Telemetry]\r\n"
                "Enabled=on\r\n"
                "MotionTrace=0\r\n"
                "ReportIntervalSeconds=7\r\n"
                "[Features]\r\n"
                "RenderUnlock=1\r\n"
                "CameraInterpolation=false\r\n"
                "MouseAxisCorrection=yes\r\n"
                "InteractionCursorInterpolation=on\r\n"
                "PresentationLimiter=invalid\r\n"
                "[Timing]\r\n"
                "TargetFPS=144\r\n"
                "[Requiem]\r\n"
                "RenderUnlock=false\r\n"
                "CameraInterpolation=true\r\n";
    }

    const pbpfps::Settings settings = pbpfps::LoadSettings(absolutePath);
    const pbpfps::Settings requiem =
        pbpfps::LoadSettings(absolutePath, L"Requiem");
    const pbpfps::Settings blackPlague =
        pbpfps::LoadSettings(absolutePath, L"BlackPlague");
    DeleteFileW(absolutePath);

    // A per-executable section overrides only the keys it names.
    if (requiem.features.renderUnlock ||
        !requiem.features.cameraInterpolation ||
        !requiem.features.mouseAxisCorrection) {
        return Fail("per-executable override section was not applied");
    }
    // A section that is absent leaves [Features] untouched.
    if (!blackPlague.features.renderUnlock ||
        blackPlague.features.cameraInterpolation) {
        return Fail("an absent override section changed the features");
    }

    if (!settings.enabled || !settings.diagnostics || !settings.telemetry) {
        return Fail("textual true values were not parsed");
    }
    if (settings.motionTrace || settings.features.cameraInterpolation) {
        return Fail("false values were not parsed");
    }
    if (!settings.features.renderUnlock ||
        !settings.features.mouseAxisCorrection ||
        !settings.features.interactionCursorInterpolation) {
        return Fail("numeric/textual feature true value was not parsed");
    }
    if (settings.features.presentationLimiter) {
        return Fail("invalid value did not fall back to false");
    }
    if (settings.reportIntervalSeconds != 7 || settings.targetFps != 144) {
        return Fail("integer values were not parsed");
    }

    std::cout << "config_tests: PASS\n";
    return 0;
}
