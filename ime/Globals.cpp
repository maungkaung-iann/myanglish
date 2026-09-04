#include "Globals.h"

#include <algorithm>
#include <array>
#include <cctype>
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

    // Alpha 0.9.0: normalize every UTF-16 string to Unicode NFC before it is
    // shown inside a TSF composition. Myanmar combining marks can be stored in
    // more than one canonically equivalent order (for example U+103A/U+1037).
    // Some editors shape the non-normalized order incorrectly while the text is
    // still composing, even though it looks correct after commit. NFC gives all
    // hosts the same canonical mark order without app-specific exceptions.
    const int normalizedSize = NormalizeString(
        NormalizationC,
        converted.data(),
        convertedSize,
        nullptr,
        0
    );
    if (normalizedSize > 0) {
        std::wstring normalized(static_cast<std::size_t>(normalizedSize), L'\0');
        const int written = NormalizeString(
            NormalizationC,
            converted.data(),
            convertedSize,
            normalized.data(),
            normalizedSize
        );
        if (written > 0) {
            normalized.resize(static_cast<std::size_t>(written));
            return normalized;
        }
    }

    // Normalization failure must never break typing; the valid UTF-16 text is
    // still safe to use as a fallback.
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

std::filesystem::path userDictionaryPath() {
    const std::filesystem::path localAppData = environmentPath(L"LOCALAPPDATA");
    if (!localAppData.empty()) {
        return localAppData / "MyanglishIME" / "user_dictionary.csv";
    }

    return moduleDirectory() / "user_dictionary.csv";
}

std::filesystem::path userHistoryPath() {
    const std::filesystem::path localAppData = environmentPath(L"LOCALAPPDATA");
    if (!localAppData.empty()) {
        return localAppData / "MyanglishIME" / "user_history.csv";
    }

    return moduleDirectory() / "user_history.csv";
}


std::filesystem::path settingsPath() {
    const std::filesystem::path localAppData = environmentPath(L"LOCALAPPDATA");
    if (!localAppData.empty()) {
        return localAppData / "MyanglishIME" / "settings.ini";
    }

    return moduleDirectory() / "settings.ini";
}

bool liveCandidatePopupEnabled() noexcept {
    // alpha-0.8.2 keeps the inline TSF composition raw until explicit commit.
    // This setting controls whether the live candidate popup is visible while
    // typing. Default is ON.
    try {
        std::ifstream file(settingsPath(), std::ios::binary);
        if (!file.is_open()) {
            return true;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            const auto separator = line.find('=');
            if (separator == std::string::npos) {
                continue;
            }

            std::string key = line.substr(0, separator);
            std::string value = line.substr(separator + 1);

            auto normalize = [](std::string& text) {
                text.erase(
                    std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
                        return ch == ' ' || ch == '\t';
                    }),
                    text.end()
                );
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
            };

            normalize(key);
            normalize(value);
            if (key == "live_candidates") {
                return value == "1" || value == "true" || value == "on" || value == "yes";
            }
        }
    } catch (...) {
        // A broken/missing settings file must never break text input.
    }

    return true;
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
