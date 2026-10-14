#include "target_host.h"

namespace pbpfps::target {

bool SelectAdapterForImage(const ImageView& image,
                           Selection& selection) noexcept {
    selection = {};
    selection.timestamp = image.timestamp;
    selection.imageSize = image.imageSize;

    const Adapter* accepted = nullptr;
    Mapping acceptedMapping{};
    std::size_t acceptedCount = 0;
    std::wstring reasons;
    for (const Adapter* const adapter : Adapters()) {
        // Each adapter resolves into its own mapping, so a partial match is
        // discarded whole and never seeds another adapter's result.
        Mapping mapping{};
        std::wstring error;
        if (ResolveSignatures(image, *adapter, mapping, error)) {
            ++acceptedCount;
            accepted = adapter;
            acceptedMapping = mapping;
            continue;
        }
        if (!reasons.empty()) {
            reasons += L"; ";
        }
        reasons += std::wstring(adapter->name) + L": " + error;
    }

    if (acceptedCount == 0) {
        selection.error = reasons;
        return false;
    }
    if (acceptedCount != 1) {
        selection.error =
            L"more than one adapter matched this image; refusing to choose";
        return false;
    }
    selection.adapter = accepted;
    selection.mapping = acceptedMapping;
    return true;
}

bool SelectAdapter(const HMODULE executable, Selection& selection) noexcept {
    selection = {};
    ImageView image{};
    if (!OpenImage(executable, image, selection.error)) {
        return false;
    }
    if (!SelectAdapterForImage(image, selection)) {
        return false;
    }
    std::wstring error;
    if (!ResolveImports(image, selection.mapping, error)) {
        selection.adapter = nullptr;
        selection.mapping = {};
        selection.error = error;
        return false;
    }
    return true;
}

} // namespace pbpfps::target
