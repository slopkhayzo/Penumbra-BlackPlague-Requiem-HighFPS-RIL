#include "pattern_scan.h"

#include <algorithm>

namespace pbpfps::pattern {

UniqueMatch FindUnique(const std::span<const std::uint8_t> haystack,
                       const std::span<const std::uint8_t> needle,
                       const std::string_view mask) noexcept {
    UniqueMatch result{};
    if (needle.empty() || needle.size() > haystack.size() ||
        (!mask.empty() && mask.size() != needle.size())) {
        return result;
    }

    for (std::size_t offset = 0; offset <= haystack.size() - needle.size();
         ++offset) {
        bool matches = true;
        for (std::size_t index = 0; index < needle.size(); ++index) {
            if ((mask.empty() || mask[index] == 'x') &&
                haystack[offset + index] != needle[index]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            result.offset = offset;
            ++result.count;
            if (result.count > 1) {
                return result;
            }
        }
    }
    return result;
}

} // namespace pbpfps::pattern
