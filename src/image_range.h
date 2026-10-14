#pragma once

#include <Windows.h>

#include <cstdint>

// Address-range checks against the host executable's mapped image. The range
// is read from the PE headers once; nothing here issues a page query.
namespace pbpfps::memory {

struct AddressRange {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
};

// Union of the main module's executable sections, or an empty range when the
// headers are not a PE32 image.
[[nodiscard]] inline AddressRange MainImageCodeRange() noexcept {
    AddressRange range{};
    const auto* const base =
        reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        return range;
    }
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 ||
        dos->e_lfanew > 0x1000) {
        return range;
    }
    const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        base + static_cast<std::uint32_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return range;
    }
    const std::uintptr_t imageBegin = reinterpret_cast<std::uintptr_t>(base);
    const std::uintptr_t imageEnd = imageBegin + nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER* const sections = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        if ((sections[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
            sections[index].Misc.VirtualSize == 0) {
            continue;
        }
        const std::uintptr_t begin =
            imageBegin + sections[index].VirtualAddress;
        const std::uintptr_t end = begin + sections[index].Misc.VirtualSize;
        if (begin < imageBegin || end > imageEnd || end < begin) {
            continue;
        }
        if (range.end == 0) {
            range = {begin, end};
        } else {
            range.begin = begin < range.begin ? begin : range.begin;
            range.end = end > range.end ? end : range.end;
        }
    }
    return range;
}

[[nodiscard]] inline bool IsMainImageCode(const void* const address) noexcept {
    static const AddressRange range = MainImageCodeRange();
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= range.begin && value < range.end;
}

} // namespace pbpfps::memory
