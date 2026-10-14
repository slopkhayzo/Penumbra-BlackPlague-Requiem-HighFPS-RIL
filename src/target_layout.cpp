#include "target_layout.h"

#include <cstring>

namespace pbpfps::target {
namespace {

Layout g_layout{};
bool g_selected = false;

} // namespace

const Layout& ActiveLayout() noexcept { return g_layout; }

bool HasActiveLayout() noexcept { return g_selected; }

bool SelectLayout(const Layout& layout) noexcept {
    if (g_selected) {
        return std::memcmp(&g_layout, &layout, sizeof(Layout)) == 0;
    }
    // Every member is four bytes wide on x86, so the table has no padding
    // and the bytewise comparison above is exact.
    std::memcpy(&g_layout, &layout, sizeof(Layout));
    g_selected = true;
    return true;
}

} // namespace pbpfps::target
