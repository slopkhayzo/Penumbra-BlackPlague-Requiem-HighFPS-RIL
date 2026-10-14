#include "host_validation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace pbpfps::host {
namespace {

bool RvaRangeValid(const std::uint32_t rva, const std::size_t bytes,
                   const std::uint32_t imageSize) noexcept {
    return rva < imageSize && bytes <= imageSize - rva;
}

char LowerAscii(const char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                       : value;
}

bool RvaStringEqualsInsensitive(const std::uint8_t* const base,
                                const std::uint32_t rva,
                                const std::uint32_t imageSize,
                                const char* const expected) noexcept {
    if (base == nullptr || expected == nullptr || rva >= imageSize) {
        return false;
    }
    const char* const value = reinterpret_cast<const char*>(base + rva);
    const std::size_t maximum = imageSize - rva;
    for (std::size_t index = 0; index < maximum; ++index) {
        if (LowerAscii(value[index]) != LowerAscii(expected[index])) {
            return false;
        }
        if (expected[index] == '\0') {
            return true;
        }
    }
    return false;
}

std::wstring MainImagePath() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

} // namespace

Report ValidateMainImage() noexcept {
    Report report{};
    report.imagePath = MainImagePath();
    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        report.error = L"GetModuleHandleW(nullptr) failed";
        return report;
    }
    const auto* const base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 ||
        dos->e_lfanew > 0x1000 - static_cast<LONG>(sizeof(IMAGE_NT_HEADERS32))) {
        report.error = L"main module does not have a valid DOS header";
        return report;
    }
    const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        base + static_cast<std::uint32_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        report.error = L"main module is not a PE32 x86 image";
        return report;
    }

    report.imageBase = reinterpret_cast<std::uintptr_t>(base);
    report.imageSize = nt->OptionalHeader.SizeOfImage;
    report.timestamp = nt->FileHeader.TimeDateStamp;
    if (report.imageSize < sizeof(IMAGE_DOS_HEADER) ||
        nt->OptionalHeader.SizeOfHeaders > report.imageSize ||
        nt->FileHeader.NumberOfSections == 0 ||
        nt->FileHeader.NumberOfSections > 96) {
        report.error = L"main image size is implausibly small";
        return report;
    }

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    const auto sectionOffset = static_cast<std::size_t>(
        reinterpret_cast<const std::uint8_t*>(section) - base);
    const std::size_t sectionBytes =
        static_cast<std::size_t>(nt->FileHeader.NumberOfSections) *
        sizeof(IMAGE_SECTION_HEADER);
    if (sectionOffset > nt->OptionalHeader.SizeOfHeaders ||
        sectionBytes > nt->OptionalHeader.SizeOfHeaders - sectionOffset) {
        report.error = L"section table is outside the PE headers";
        return report;
    }
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        char name[IMAGE_SIZEOF_SHORT_NAME + 1]{};
        std::memcpy(name, section[index].Name, IMAGE_SIZEOF_SHORT_NAME);
        const bool inImage = RvaRangeValid(
            section[index].VirtualAddress,
            static_cast<std::size_t>(section[index].Misc.VirtualSize),
            report.imageSize);
        if (!inImage || section[index].Misc.VirtualSize == 0) {
            continue;
        }
        if (std::strcmp(name, ".text") == 0 &&
            (section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
            report.hasText = true;
        } else if (std::strcmp(name, ".rdata") == 0 &&
                   (section[index].Characteristics & IMAGE_SCN_MEM_READ) != 0) {
            report.hasRdata = true;
        } else if (std::strcmp(name, ".data") == 0 &&
                   (section[index].Characteristics & IMAGE_SCN_MEM_WRITE) != 0) {
            report.hasData = true;
        }
    }

    const IMAGE_DATA_DIRECTORY& imports =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!RvaRangeValid(imports.VirtualAddress, sizeof(IMAGE_IMPORT_DESCRIPTOR),
                       report.imageSize)) {
        report.error = L"main image import directory is invalid";
        return report;
    }

    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        base + imports.VirtualAddress);
    const std::size_t maximumDescriptors = std::min<std::size_t>(
        256, imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR));
    for (std::size_t descriptorIndex = 0;
         descriptorIndex < maximumDescriptors && descriptor->Name != 0;
         ++descriptorIndex, ++descriptor) {
        if (!RvaRangeValid(descriptor->Name, 1, report.imageSize)) {
            report.error = L"import descriptor contains an invalid module name";
            return report;
        }
        const bool isSdl = RvaStringEqualsInsensitive(
            base, descriptor->Name, report.imageSize, "SDL.dll");
        report.importsSdl = report.importsSdl || isSdl;
        report.importsNewton = report.importsNewton || RvaStringEqualsInsensitive(
            base, descriptor->Name, report.imageSize, "Newton.dll");

        if (!isSdl) {
            continue;
        }
        const DWORD thunkRva = descriptor->OriginalFirstThunk != 0
                                   ? descriptor->OriginalFirstThunk
                                   : descriptor->FirstThunk;
        if (!RvaRangeValid(thunkRva, sizeof(IMAGE_THUNK_DATA32),
                           report.imageSize)) {
            report.error = L"SDL import thunk is outside the image";
            return report;
        }
        const auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
            base + thunkRva);
        constexpr std::size_t kMaxThunks = 4096;
        for (std::size_t thunkIndex = 0;
             thunkIndex < kMaxThunks && thunk->u1.AddressOfData != 0;
             ++thunkIndex, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL32(thunk->u1.Ordinal)) {
                continue;
            }
            const DWORD nameRva = thunk->u1.AddressOfData;
            if (!RvaRangeValid(nameRva, sizeof(IMAGE_IMPORT_BY_NAME),
                               report.imageSize)) {
                report.error = L"SDL import name is outside the image";
                return report;
            }
            const DWORD stringRva =
                nameRva + static_cast<DWORD>(offsetof(IMAGE_IMPORT_BY_NAME, Name));
            report.importsSdlTicks = report.importsSdlTicks ||
                RvaStringEqualsInsensitive(base, stringRva, report.imageSize,
                                           "SDL_GetTicks");
            report.importsSdlSwap = report.importsSdlSwap ||
                RvaStringEqualsInsensitive(base, stringRva, report.imageSize,
                                           "SDL_GL_SwapBuffers");
            report.importsSdlPoll = report.importsSdlPoll ||
                RvaStringEqualsInsensitive(base, stringRva, report.imageSize,
                                           "SDL_PollEvent");
            report.importsSdlRelativeMouse =
                report.importsSdlRelativeMouse ||
                RvaStringEqualsInsensitive(base, stringRva, report.imageSize,
                                           "SDL_GetRelativeMouseState");
        }
    }

    if (!report.hasText || !report.hasRdata || !report.hasData) {
        report.error = L"required executable sections were not found";
        return report;
    }
    if (!report.importsSdl || !report.importsNewton || !report.importsSdlTicks ||
        !report.importsSdlSwap || !report.importsSdlPoll ||
        !report.importsSdlRelativeMouse) {
        report.error = L"required HPL1 SDL/Newton imports were not found";
        return report;
    }
    report.compatibleForDiagnostics = true;
    return report;
}

} // namespace pbpfps::host
