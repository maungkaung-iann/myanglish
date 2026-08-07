#include "Globals.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace myanglish::ime {

namespace {

std::atomic<long> g_serverLockCount{0};
std::atomic<long> g_objectCount{0};
HMODULE g_moduleHandle = nullptr;

bool hasDataFiles(const std::filesystem::path& root) {
    std::error_code errorCode;
    return std::filesystem::exists(root / "data" / "dictionary.csv", errorCode)
        && std::filesystem::exists(root / "data" / "rules" / "rhymes.csv", errorCode)
        && std::filesystem::exists(root / "data" / "rules" / "tone_marks.csv", errorCode);
}

std::filesystem::path environmentPath(const wchar_t* name) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD size = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) {
        return {};
    }

    return std::filesystem::path(buffer.data());
}

std::filesystem::path currentModuleDirectory() {
    if (g_moduleHandle == nullptr) {
        return std::filesystem::current_path();
    }

    std::array<wchar_t, 32768> buffer{};
    const DWORD size = GetModuleFileNameW(g_moduleHandle, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) {
        return std::filesystem::current_path();
    }

    return std::filesystem::path(buffer.data()).parent_path();
}

} // namespace

void setModuleHandle(HMODULE handle) noexcept {
    g_moduleHandle = handle;
}

HMODULE moduleHandle() noexcept {
    return g_moduleHandle;
}

void addServerLock() noexcept {
    ++g_serverLockCount;
}

void releaseServerLock() noexcept {
    --g_serverLockCount;
}

long serverLockCount() noexcept {
    return g_serverLockCount.load();
}

void addObject() noexcept {
    ++g_objectCount;
}

void releaseObject() noexcept {
    --g_objectCount;
}

long objectCount() noexcept {
    return g_objectCount.load();
}

std::wstring utf8ToUtf16(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int requiredSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );

    if (requiredSize <= 0) {
        return {};
    }

    std::wstring converted(static_cast<std::size_t>(requiredSize), L'\0');
    const int convertedSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        converted.data(),
        requiredSize
    );

    if (convertedSize <= 0) {
        return {};
    }

    return converted;
}

std::string utf16ToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (requiredSize <= 0) {
        return {};
    }

    std::string converted(static_cast<std::size_t>(requiredSize), '\0');
    const int convertedSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        converted.data(),
        requiredSize,
        nullptr,
        nullptr
    );

    if (convertedSize <= 0) {
        return {};
    }

    return converted;
}

std::filesystem::path moduleDirectory() {
    return currentModuleDirectory();
}

std::filesystem::path resolveDataRoot() {
    const std::filesystem::path moduleRoot = moduleDirectory();
    if (hasDataFiles(moduleRoot)) {
        return moduleRoot;
    }

    const std::filesystem::path programData = environmentPath(L"ProgramData");
    if (!programData.empty()) {
        const std::filesystem::path installedRoot = programData / "MyanglishIME";
        if (hasDataFiles(installedRoot)) {
            return installedRoot;
        }
    }

#ifdef MYANGLISHIME_SOURCE_DIR
    const std::filesystem::path sourceRoot = std::filesystem::path(MYANGLISHIME_SOURCE_DIR);
    if (hasDataFiles(sourceRoot)) {
        return sourceRoot;
    }
    return sourceRoot;
#else
    return moduleRoot;
#endif
}

std::filesystem::path resolveDictionaryPath() {
    return resolveDataRoot() / "data" / "dictionary.csv";
}

std::filesystem::path debugLogPath() {
    const std::filesystem::path localAppData = environmentPath(L"LOCALAPPDATA");
    if (!localAppData.empty()) {
        return localAppData / "MyanglishIME" / "debug.log";
    }

    return moduleDirectory() / "MyanglishIME-debug.log";
}

void debugLog(std::string_view message) noexcept {
#ifdef MYANGLISHIME_ENABLE_DEBUG_LOG
    try {
        const DWORD processId = GetCurrentProcessId();
        const std::string line =
            "[MyanglishIME pid=" + std::to_string(processId) + "] " +
            std::string(message) + "\n";

        OutputDebugStringA(line.c_str());

        const std::filesystem::path logPath = debugLogPath();
        std::error_code errorCode;
        std::filesystem::create_directories(logPath.parent_path(), errorCode);

        std::ofstream file(logPath, std::ios::binary | std::ios::app);
        if (file.is_open()) {
            file.write(line.data(), static_cast<std::streamsize>(line.size()));
        }
    } catch (...) {
        // Debug logging must never destabilize an application hosting the TIP.
    }
#else
    (void)message;
#endif
}

void debugLogHr(std::string_view operation, HRESULT hr) noexcept {
#ifdef MYANGLISHIME_ENABLE_DEBUG_LOG
    try {
        std::ostringstream stream;
        stream << operation << " hr=0x"
               << std::hex << std::uppercase
               << static_cast<unsigned long>(hr);
        debugLog(stream.str());
    } catch (...) {
    }
#else
    (void)operation;
    (void)hr;
#endif
}

} // namespace myanglish::ime
