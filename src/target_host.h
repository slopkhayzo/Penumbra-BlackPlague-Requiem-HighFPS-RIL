#pragma once

#include "target_adapter.h"
#include "target_mapping.h"

#include <Windows.h>

#include <string>

// Identifies the host executable from its mapped image and chooses the one
// adapter whose complete signature and layout-witness scan passes. File name,
// timestamp, and size are reported for support but never decide anything.
namespace pbpfps::target {

struct Selection {
    const Adapter* adapter = nullptr;
    Mapping mapping{};
    std::uint32_t timestamp = 0;
    std::uint32_t imageSize = 0;
    // First precise reason, one clause per rejected adapter.
    std::wstring error;
};

// Signature scan only; usable on an image mapped from disk. Succeeds only
// when exactly one adapter passes completely. A partial match of one adapter
// never contributes anything to another.
[[nodiscard]] bool SelectAdapterForImage(const ImageView& image,
                                         Selection& selection) noexcept;

// Full gate for the live process: signature scan plus bound imports.
[[nodiscard]] bool SelectAdapter(HMODULE executable,
                                 Selection& selection) noexcept;

} // namespace pbpfps::target
