#include "logger.h"

#include <array>

namespace pbpfps::logger {
namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_path;

std::wstring ModulePath(const HMODULE module) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(module, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

bool OpenAt(const std::wstring& path) noexcept {
    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    g_file = file;
    g_path = path;
    return true;
}

} // namespace

bool Initialize(const HMODULE pluginModule) noexcept {
    const std::wstring modulePath = ModulePath(pluginModule);
    const std::wstring sibling =
        DirectoryOf(modulePath) + L"\\PenumbraBlackPlague.FPSFix.log";
    if (!modulePath.empty() && OpenAt(sibling)) {
        return true;
    }

    std::array<wchar_t, 32768> localAppData{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData.data(),
        static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size()) {
        return false;
    }
    const std::wstring directory =
        std::wstring(localAppData.data(), length) + L"\\PenumbraBlackPlague.FPSFix";
    if (!CreateDirectoryW(directory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    return OpenAt(directory + L"\\PenumbraBlackPlague.FPSFix.log");
}

void Write(const std::wstring& message) noexcept {
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t prefix[64]{};
    const int prefixLength = swprintf_s(
        prefix, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ", time.wYear,
        time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds);
    std::wstring line;
    if (prefixLength > 0) {
        line.assign(prefix, static_cast<std::size_t>(prefixLength));
    }
    line += message;
    line += L"\r\n";

    const int bytesNeeded = WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                                                static_cast<int>(line.size()),
                                                nullptr, 0, nullptr, nullptr);
    if (bytesNeeded <= 0) {
        return;
    }
    std::string utf8(static_cast<std::size_t>(bytesNeeded), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                            static_cast<int>(line.size()), utf8.data(),
                            bytesNeeded, nullptr, nullptr) != bytesNeeded) {
        return;
    }
    // No FlushFileBuffers here: reports are written from the render thread
    // and a synchronous disk flush per line stalls it for milliseconds. Data
    // handed to WriteFile already survives a crash of the game process.
    DWORD written = 0;
    WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
              nullptr);
}

void Shutdown() noexcept {
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

const std::wstring& Path() noexcept { return g_path; }

} // namespace pbpfps::logger
