#include "config.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>

namespace pbpfps {
namespace {

bool ReadBool(const std::wstring& path, const wchar_t* section,
              const wchar_t* key, const bool fallback) noexcept {
    std::array<wchar_t, 32> buffer{};
    const wchar_t* defaultValue = fallback ? L"true" : L"false";
    const DWORD length = GetPrivateProfileStringW(
        section, key, defaultValue, buffer.data(),
        static_cast<DWORD>(buffer.size()), path.c_str());

    std::wstring value(buffer.data(), length);
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [](const wchar_t ch) {
                                                return !std::iswspace(ch);
                                            }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [](const wchar_t ch) {
                                 return !std::iswspace(ch);
                             }).base(),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const wchar_t ch) {
                       return static_cast<wchar_t>(std::towlower(ch));
                   });

    if (value == L"1" || value == L"true" || value == L"yes" ||
        value == L"on") {
        return true;
    }
    if (value == L"0" || value == L"false" || value == L"no" ||
        value == L"off") {
        return false;
    }
    return fallback;
}

bool HasKey(const std::wstring& path, const wchar_t* section,
            const wchar_t* key) noexcept {
    // A value can never contain a newline, so this default is only returned
    // when the key is absent.
    constexpr wchar_t kMissing[] = L"\n";
    std::array<wchar_t, 32> buffer{};
    const DWORD length = GetPrivateProfileStringW(
        section, key, kMissing, buffer.data(),
        static_cast<DWORD>(buffer.size()), path.c_str());
    return !(length == 1 && buffer[0] == L'\n');
}

} // namespace

Settings LoadSettings(const std::wstring& path,
                      const wchar_t* const overrideSection) noexcept {
    Settings result{};
    result.enabled = ReadBool(path, L"General", L"Enabled", true);
    result.diagnostics = ReadBool(path, L"General", L"Diagnostics", true);
    result.telemetry = ReadBool(path, L"Telemetry", L"Enabled", true);
    result.motionTrace = ReadBool(path, L"Telemetry", L"MotionTrace", false);
    result.reportIntervalSeconds = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(
            L"Telemetry", L"ReportIntervalSeconds", 5, path.c_str())),
        1, 3600);

    const auto feature = [&](const wchar_t* const key) noexcept {
        const bool base = ReadBool(path, L"Features", key, false);
        if (overrideSection != nullptr && HasKey(path, overrideSection, key)) {
            return ReadBool(path, overrideSection, key, base);
        }
        return base;
    };
    FeatureSettings& f = result.features;
    f.renderUnlock = feature(L"RenderUnlock");
    f.presentationLimiter = feature(L"PresentationLimiter");
    f.cameraInterpolation = feature(L"CameraInterpolation");
    f.handsRootInterpolation = feature(L"HandsRootInterpolation");
    f.worldRootInterpolation = feature(L"WorldRootInterpolation");
    f.handsPoseInterpolation = feature(L"HandsPoseInterpolation");
    f.worldPoseInterpolation = feature(L"WorldPoseInterpolation");
    f.ragdollPoseInterpolation = feature(L"RagdollPoseInterpolation");
    f.particleInterpolation = feature(L"ParticleInterpolation");
    f.imageTrailTiming = feature(L"ImageTrailTiming");
    f.motionBlurTiming = feature(L"MotionBlurTiming");
    f.mouseOverlay = feature(L"MouseOverlay");
    f.mouseAxisCorrection = feature(L"MouseAxisCorrection");
    f.interactionCursorInterpolation = feature(L"InteractionCursorInterpolation");
    f.guiMouseOverlay = feature(L"GuiMouseOverlay");

    result.targetFps = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(
            L"Timing", L"TargetFPS", 0, path.c_str())),
        0, 1000);
    return result;
}

bool AnyBehaviorFeatureEnabled(const FeatureSettings& f) noexcept {
    return f.renderUnlock || f.presentationLimiter || f.cameraInterpolation ||
           f.handsRootInterpolation || f.worldRootInterpolation ||
           f.handsPoseInterpolation || f.worldPoseInterpolation ||
           f.ragdollPoseInterpolation || f.particleInterpolation ||
           f.imageTrailTiming || f.motionBlurTiming || f.mouseOverlay ||
           f.mouseAxisCorrection || f.interactionCursorInterpolation ||
           f.guiMouseOverlay;
}

} // namespace pbpfps
