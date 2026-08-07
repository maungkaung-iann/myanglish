#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <Windows.h>

namespace myanglish::ime {

void setModuleHandle(HMODULE moduleHandle) noexcept;
HMODULE moduleHandle() noexcept;

void addServerLock() noexcept;
void releaseServerLock() noexcept;
long serverLockCount() noexcept;

void addObject() noexcept;
void releaseObject() noexcept;
long objectCount() noexcept;

std::wstring utf8ToUtf16(const std::string& text);
std::string utf16ToUtf8(std::wstring_view text);

std::filesystem::path moduleDirectory();
std::filesystem::path resolveDataRoot();
std::filesystem::path resolveDictionaryPath();
std::filesystem::path debugLogPath();

void debugLog(std::string_view message) noexcept;
void debugLogHr(std::string_view operation, HRESULT hr) noexcept;

} // namespace myanglish::ime
