#include <Windows.h>
#include <mmsystem.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

std::string ReadLog() {
    std::ifstream log("PenumbraBlackPlague.FPSFix.log", std::ios::binary);
    if (!log) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(log)),
                       std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const MMRESULT begin = timeBeginPeriod(1);
    const DWORD first = timeGetTime();
    Sleep(20);
    const DWORD second = timeGetTime();
    if (begin == TIMERR_NOERROR) {
        timeEndPeriod(1);
    }
    if (second < first || second - first > 5000) {
        std::cerr << "FAIL: proxied WinMM timer did not advance plausibly\n";
        return 1;
    }

    constexpr DWORD kWaitMilliseconds = 6000;
    constexpr DWORD kPollMilliseconds = 25;
    DWORD elapsed = 0;
    std::string contents;
    while (elapsed < kWaitMilliseconds) {
        contents = ReadLog();
        if (Contains(contents, "Host validation failed closed")) {
            break;
        }
        Sleep(kPollMilliseconds);
        elapsed += kPollMilliseconds;
    }

    if (contents.empty()) {
        std::cerr << "FAIL: Ultimate ASI Loader did not load the diagnostic ASI\n";
        return 1;
    }
    if (!Contains(contents, "diagnostic bootstrap starting") ||
        !Contains(contents, "Host validation failed closed")) {
        std::cerr << "FAIL: diagnostic ASI did not reject the synthetic host as expected\n";
        return 1;
    }
    std::cout << "asi_loader_smoke_host: PASS\n";
    return 0;
}
