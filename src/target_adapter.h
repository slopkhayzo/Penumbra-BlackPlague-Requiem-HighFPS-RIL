#pragma once

#include "target_layout.h"
#include "target_mapping.h"

#include <cstddef>
#include <cstdint>
#include <span>

// One adapter per supported executable: the masked signatures that identify
// it and the layout table its code was compiled against. An adapter is
// accepted only as a whole; nothing is ever combined across adapters.
namespace pbpfps::target {

enum class Match : std::uint8_t {
    unique,     // exactly one match in .text
    atLeastOne, // tiny folded accessors: presence is the evidence
};

struct Signature {
    const wchar_t* name;
    const std::uint8_t* bytes;
    const char* mask; // 'x' literal byte, '?' image address byte
    std::size_t size;
    void* Mapping::*member; // nullptr: layout witness, no address kept
    Match match;
};

enum class AdapterId : std::uint8_t {
    blackPlague,
    requiem,
};

struct Adapter {
    AdapterId id;
    const wchar_t* name;
    // INI section whose keys override [Features] for this executable only.
    const wchar_t* overrideSection;
    // Support identifiers for the log. They are not part of the gate.
    const wchar_t* referenceFileName;
    std::uint32_t referenceTimestamp;
    std::uint32_t referenceImageSize;
    std::span<const Signature> gameSignatures;
    const Layout* layout;
};

// Shared HPL1 engine-layer signatures; required by every adapter.
[[nodiscard]] std::span<const Signature> EngineSignatures() noexcept;

[[nodiscard]] const Adapter& BlackPlagueAdapter() noexcept;
[[nodiscard]] const Adapter& RequiemAdapter() noexcept;
[[nodiscard]] std::span<const Adapter* const> Adapters() noexcept;

} // namespace pbpfps::target
