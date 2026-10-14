#include "target_mapping.h"

#include "pattern_scan.h"
#include "target_adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace pbpfps::target {
namespace {

#include "target_engine_signatures.inc"

bool RangeIsInImage(const std::size_t imageSize, const std::size_t offset,
                    const std::size_t size) noexcept {
    return offset <= imageSize && size <= imageSize - offset;
}

bool ResolveImport(const ImageView& image,
                   const char* importedName, const wchar_t* diagnosticName,
                   void*& result, std::wstring& error) noexcept {
    const std::uint8_t* const imageBase = image.base;
    const std::size_t imageSize = image.imageSize;
    const IMAGE_DATA_DIRECTORY imports{image.importDirectoryRva,
                                       image.importDirectorySize};
    if (!RangeIsInImage(imageSize, imports.VirtualAddress, imports.Size) ||
        imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        error = L"Executable import directory is invalid";
        return false;
    }

    const auto* descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        imageBase + imports.VirtualAddress);
    const std::size_t descriptorCount = imports.Size / sizeof(*descriptors);
    std::size_t matches = 0;
    for (std::size_t descriptorIndex = 0; descriptorIndex < descriptorCount;
         ++descriptorIndex) {
        const IMAGE_IMPORT_DESCRIPTOR& descriptor = descriptors[descriptorIndex];
        if (descriptor.Name == 0 && descriptor.FirstThunk == 0) {
            break;
        }
        if (descriptor.OriginalFirstThunk == 0 || descriptor.FirstThunk == 0) {
            continue;
        }
        for (std::size_t index = 0; index < 4096; ++index) {
            const std::size_t nameOffset = descriptor.OriginalFirstThunk +
                index * sizeof(IMAGE_THUNK_DATA32);
            const std::size_t iatOffset = descriptor.FirstThunk +
                index * sizeof(IMAGE_THUNK_DATA32);
            if (!RangeIsInImage(imageSize, nameOffset, sizeof(IMAGE_THUNK_DATA32)) ||
                !RangeIsInImage(imageSize, iatOffset, sizeof(IMAGE_THUNK_DATA32))) {
                error = std::wstring(diagnosticName) + L": import thunk is outside image";
                return false;
            }
            const auto* nameThunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
                imageBase + nameOffset);
            const auto* iatThunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
                imageBase + iatOffset);
            if (nameThunk->u1.AddressOfData == 0) {
                break;
            }
            if (IMAGE_SNAP_BY_ORDINAL32(nameThunk->u1.Ordinal)) {
                continue;
            }
            const std::size_t importOffset = nameThunk->u1.AddressOfData;
            if (!RangeIsInImage(imageSize, importOffset,
                                sizeof(IMAGE_IMPORT_BY_NAME))) {
                error = std::wstring(diagnosticName) + L": import name is outside image";
                return false;
            }
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                imageBase + importOffset);
            const char* name = reinterpret_cast<const char*>(import->Name);
            const std::size_t stringOffset =
                static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(name) -
                                         imageBase);
            if (std::memchr(name, '\0', imageSize - stringOffset) == nullptr) {
                error = std::wstring(diagnosticName) + L": import name is unterminated";
                return false;
            }
            if (std::strcmp(name, importedName) == 0) {
                result = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(iatThunk->u1.Function));
                ++matches;
            }
        }
    }
    if (matches != 1 || result == nullptr) {
        error = std::wstring(diagnosticName) + L": expected one import, found " +
                std::to_wstring(matches);
        return false;
    }
    return true;
}


bool ResolveOne(const ImageView& image, const Signature& signature,
                Mapping& mapping, std::wstring& error) noexcept {
    const std::span<const std::uint8_t> text(image.text, image.textSize);
    const pattern::UniqueMatch match = pattern::FindUnique(
        text, std::span<const std::uint8_t>(signature.bytes, signature.size),
        std::string_view(signature.mask, signature.size));
    const bool accepted = signature.match == Match::unique ? match.count == 1
                                                           : match.count >= 1;
    if (!accepted) {
        error = std::wstring(signature.name) +
                (signature.match == Match::unique
                     ? L": expected one signature match, found "
                     : L": expected at least one signature match, found ") +
                (match.count > 1 ? L"more than one"
                                 : std::to_wstring(match.count));
        return false;
    }
    if (signature.member != nullptr) {
        mapping.*(signature.member) =
            const_cast<std::uint8_t*>(image.text + match.offset);
    }
    return true;
}

} // namespace

std::span<const Signature> EngineSignatures() noexcept {
    return kEngineSignatures;
}

std::span<const Adapter* const> Adapters() noexcept {
    static const Adapter* const adapters[] = {
        &BlackPlagueAdapter(),
        &RequiemAdapter(),
    };
    return adapters;
}

bool OpenImage(const HMODULE executable, ImageView& image,
               std::wstring& error) noexcept {
    image = {};
    if (executable == nullptr) {
        error = L"Main executable module is unavailable";
        return false;
    }
    // LoadLibraryEx data-file handles carry flag bits in the low two bits.
    const auto* const imageBase = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(executable) & ~std::uintptr_t{3});
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 ||
        dos->e_lfanew > 0x1000) {
        error = L"Invalid DOS header in main executable mapping";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        imageBase + static_cast<std::size_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        error = L"Invalid PE32 header in main executable mapping";
        return false;
    }

    const IMAGE_SECTION_HEADER* text = nullptr;
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        if (std::memcmp(sections[index].Name, ".text", 6) == 0) {
            text = &sections[index];
            break;
        }
    }
    if (text == nullptr || text->Misc.VirtualSize == 0 ||
        !RangeIsInImage(nt->OptionalHeader.SizeOfImage, text->VirtualAddress,
                        text->Misc.VirtualSize) ||
        (text->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
        error = L"Executable .text section is invalid";
        return false;
    }

    image.base = imageBase;
    image.imageSize = nt->OptionalHeader.SizeOfImage;
    image.timestamp = nt->FileHeader.TimeDateStamp;
    image.text = imageBase + text->VirtualAddress;
    image.textSize = text->Misc.VirtualSize;
    const IMAGE_DATA_DIRECTORY& imports =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    image.importDirectoryRva = imports.VirtualAddress;
    image.importDirectorySize = imports.Size;
    return true;
}

bool ResolveSignatures(const ImageView& image, const Adapter& adapter,
                       Mapping& mapping, std::wstring& error) noexcept {
    mapping = {};
    if (image.text == nullptr || image.textSize == 0) {
        error = L"Image view is not open";
        return false;
    }
    for (const Signature& signature : EngineSignatures()) {
        if (!ResolveOne(image, signature, mapping, error)) {
            mapping = {};
            return false;
        }
    }
    for (const Signature& signature : adapter.gameSignatures) {
        if (!ResolveOne(image, signature, mapping, error)) {
            mapping = {};
            return false;
        }
    }
    return true;
}

bool ResolveImports(const ImageView& image, Mapping& mapping,
                    std::wstring& error) noexcept {
    return ResolveImport(image, "SDL_GetTicks", L"SDL_GetTicks",
                         mapping.sdlGetTicks, error) &&
           ResolveImport(image, "SDL_GL_SwapBuffers", L"SDL_GL_SwapBuffers",
                         mapping.sdlGlSwapBuffers, error) &&
           ResolveImport(image, "SDL_PollEvent", L"SDL_PollEvent",
                         mapping.sdlPollEvent, error) &&
           ResolveImport(image, "SDL_GetRelativeMouseState",
                         L"SDL_GetRelativeMouseState",
                         mapping.sdlGetRelativeMouseState, error) &&
           ResolveImport(image, "SDL_PumpEvents", L"SDL_PumpEvents",
                         mapping.sdlPumpEvents, error) &&
           ResolveImport(image, "NewtonUpdate", L"NewtonUpdate",
                         mapping.newtonUpdate, error);
}

} // namespace pbpfps::target
