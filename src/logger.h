#pragma once

#include <Windows.h>

#include <string>

namespace pbpfps::logger {

[[nodiscard]] bool Initialize(HMODULE pluginModule) noexcept;
void Write(const std::wstring& message) noexcept;
void Shutdown() noexcept;
[[nodiscard]] const std::wstring& Path() noexcept;

} // namespace pbpfps::logger
