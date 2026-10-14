#pragma once

#include <cstddef>
#include <cstring>
#include <excpt.h>
#include <type_traits>

// Fault-guarded access to game memory.
//
// Presentation and capture paths must not issue page queries (VirtualQuery is
// a system call; per-object or per-particle use costs milliseconds per frame).
// These helpers touch the memory directly inside a structured exception
// handler instead: a valid access costs a few instructions, and a stale or
// malformed pointer still fails closed by returning false.
namespace pbpfps::memory {

// EXCEPTION_ACCESS_VIOLATION, EXCEPTION_IN_PAGE_ERROR, and
// STATUS_GUARD_PAGE_VIOLATION. Anything else is not a bad-pointer symptom and
// continues to the host's own handlers.
inline int FaultFilter(const unsigned long code) noexcept {
    return code == 0xC0000005UL || code == 0xC0000006UL ||
                   code == 0x80000001UL
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

[[nodiscard]] inline bool TryCopy(void* const destination,
                                  const void* const source,
                                  const std::size_t bytes) noexcept {
    if (destination == nullptr || source == nullptr) {
        return false;
    }
    __try {
        std::memcpy(destination, source, bytes);
        return true;
    } __except (FaultFilter(GetExceptionCode())) {
        return false;
    }
}

template <typename T>
[[nodiscard]] bool TryRead(const void* const address, T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    return TryCopy(&value, address, sizeof(T));
}

template <typename T>
[[nodiscard]] bool TryWrite(void* const address, const T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    return TryCopy(address, &value, sizeof(T));
}

template <typename T>
[[nodiscard]] bool TryReadAt(const void* const base,
                             const std::ptrdiff_t offset, T& value) noexcept {
    return base != nullptr &&
           TryRead(static_cast<const unsigned char*>(base) + offset, value);
}

template <typename T>
[[nodiscard]] bool TryWriteAt(void* const base, const std::ptrdiff_t offset,
                              const T& value) noexcept {
    return base != nullptr &&
           TryWrite(static_cast<unsigned char*>(base) + offset, value);
}

// True when the object's vtable slot `index` holds exactly `expected`.
[[nodiscard]] inline bool HasVtableEntry(const void* const object,
                                         const std::size_t index,
                                         const void* const expected) noexcept {
    const void* const* vtable = nullptr;
    const void* entry = nullptr;
    return expected != nullptr && TryReadAt(object, 0, vtable) &&
           vtable != nullptr &&
           TryReadAt(vtable,
                     static_cast<std::ptrdiff_t>(index * sizeof(void*)),
                     entry) &&
           entry == expected;
}

} // namespace pbpfps::memory
