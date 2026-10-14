#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace pbpfps::pattern {

struct UniqueMatch {
    std::size_t offset = 0;
    std::size_t count = 0;
};

[[nodiscard]] UniqueMatch FindUnique(std::span<const std::uint8_t> haystack,
                                     std::span<const std::uint8_t> needle,
                                     std::string_view mask = {}) noexcept;

} // namespace pbpfps::pattern
