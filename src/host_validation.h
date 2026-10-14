#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace pbpfps::host {

struct Report {
    bool compatibleForDiagnostics = false;
    std::wstring error;
    std::wstring imagePath;
    std::uintptr_t imageBase = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t timestamp = 0;
    bool hasText = false;
    bool hasRdata = false;
    bool hasData = false;
    bool importsSdl = false;
    bool importsNewton = false;
    bool importsSdlTicks = false;
    bool importsSdlSwap = false;
    bool importsSdlPoll = false;
    bool importsSdlRelativeMouse = false;
};

[[nodiscard]] Report ValidateMainImage() noexcept;

} // namespace pbpfps::host
