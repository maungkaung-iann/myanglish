#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <Windows.h>

namespace {

constexpr int kCheckboxId = 1001;
constexpr int kSaveButtonId = 1002;
constexpr int kMyanglishEditId = 1101;
constexpr int kMyanmarEditId = 1102;
constexpr int kAddWordButtonId = 1103;

std::filesystem::path settingsPath() {
    wchar_t buffer[32768]{};
    const DWORD size = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer))
    );
    if (size > 0 && size < std::size(buffer)) {
        return std::filesystem::path(buffer) / "MyanglishIME" / "settings.ini";
    }
    return std::filesystem::current_path() / "settings.ini";
}

std::filesystem::path userDictionaryPath() {
    wchar_t buffer[32768]{};
    const DWORD size = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer))
    );
    if (size > 0 && size < std::size(buffer)) {
        return std::filesystem::path(buffer) / "MyanglishIME" / "user_dictionary.csv";
    }
    return std::filesystem::current_path() / "user_dictionary.csv";
}

std::string utf8FromWide(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr
    );
    if (needed <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        result.data(), needed, nullptr, nullptr
    );
    return result;
}

std::wstring getText(HWND window, int id) {
    HWND control = GetDlgItem(window, id);
    if (control == nullptr) {
        return {};
    }
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    return result;
}

bool loadLiveCandidates() {
    std::ifstream file(settingsPath(), std::ios::binary);
    if (!file.is_open()) {
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "live_candidates=1" || line == "live_candidates=true" || line == "live_candidates=on") {
            return true;
        }
        if (line == "live_candidates=0" || line == "live_candidates=false" || line == "live_candidates=off") {
            return false;
        }
    }
    return true;
}

bool saveLiveCandidates(bool enabled) {
    try {
        const auto path = settingsPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << "# Myanglish IME settings\n";
        file << "live_candidates=" << (enabled ? "1" : "0") << "\n";
        return file.good();
    } catch (...) {
        return false;
    }
}

bool appendPersonalWord(const std::wstring& myanglishWide, const std::wstring& myanmarWide) {
    if (myanglishWide.empty() || myanmarWide.empty()) {
        return false;
    }

    const std::string myanglish = utf8FromWide(myanglishWide);
    const std::string myanmar = utf8FromWide(myanmarWide);
    if (myanglish.empty() || myanmar.empty()) {
        return false;
    }

    // Keep the personal CSV deliberately simple and safe.
    if (myanglish.find(',') != std::string::npos
        || myanmar.find(',') != std::string::npos
        || myanglish.find('\n') != std::string::npos
        || myanmar.find('\n') != std::string::npos) {
        return false;
    }

    try {
        const auto path = userDictionaryPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        const bool needsHeader = !std::filesystem::exists(path, ec)
            || std::filesystem::file_size(path, ec) == 0;

        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (!file.is_open()) {
            return false;
        }
        if (needsHeader) {
            file << "myanglish,burmese,frequency\n";
        }

        // Personal words intentionally outrank shipped defaults.
        file << myanglish << "," << myanmar << ",2000000\n";
        return file.good();
    } catch (...) {
        return false;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateWindowW(
            L"STATIC", L"Candidate behavior",
            WS_CHILD | WS_VISIBLE,
            24, 18, 240, 22,
            window, nullptr, nullptr, nullptr
        );

        HWND checkbox = CreateWindowW(
            L"BUTTON", L"Show candidate popup on first Space",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            24, 45, 300, 26,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCheckboxId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"ON (default): First Space opens all candidates with #1 selected. OFF: legacy second-Space popup.",
            WS_CHILD | WS_VISIBLE,
            44, 76, 500, 22,
            window, nullptr, nullptr, nullptr
        );

        SendMessageW(
            checkbox, BM_SETCHECK,
            loadLiveCandidates() ? BST_CHECKED : BST_UNCHECKED, 0
        );

        CreateWindowW(
            L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            450, 42, 100, 32,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveButtonId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC", L"Add a missing word (Level 3)",
            WS_CHILD | WS_VISIBLE,
            24, 120, 300, 24,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC", L"Myanglish:",
            WS_CHILD | WS_VISIBLE,
            24, 154, 100, 22,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            125, 150, 180, 28,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMyanglishEditId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC", L"Myanmar:",
            WS_CHILD | WS_VISIBLE,
            24, 190, 100, 22,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            125, 186, 180, 28,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMyanmarEditId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"BUTTON", L"Add personal word",
            WS_CHILD | WS_VISIBLE,
            330, 168, 220, 38,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddWordButtonId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"Personal words are stored in LocalAppData and survive updates. Switch Myanglish off/on after adding.",
            WS_CHILD | WS_VISIBLE,
            24, 232, 540, 40,
            window, nullptr, nullptr, nullptr
        );
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == kSaveButtonId) {
            HWND checkbox = GetDlgItem(window, kCheckboxId);
            const bool enabled = SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const bool saved = saveLiveCandidates(enabled);
            MessageBoxW(
                window,
                saved ? L"Settings saved." : L"Could not save settings.ini.",
                L"Myanglish IME Settings",
                MB_OK | (saved ? MB_ICONINFORMATION : MB_ICONERROR)
            );
            return 0;
        }

        if (LOWORD(wParam) == kAddWordButtonId) {
            const std::wstring raw = getText(window, kMyanglishEditId);
            const std::wstring myanmar = getText(window, kMyanmarEditId);

            if (appendPersonalWord(raw, myanmar)) {
                SetWindowTextW(GetDlgItem(window, kMyanglishEditId), L"");
                SetWindowTextW(GetDlgItem(window, kMyanmarEditId), L"");
                MessageBoxW(
                    window,
                    L"Personal word added.\n\nSwitch away from Myanglish and back to reload it.",
                    L"Myanglish IME Settings",
                    MB_OK | MB_ICONINFORMATION
                );
            } else {
                MessageBoxW(
                    window,
                    L"Enter both Myanglish and Myanmar text. Commas/newlines are not allowed.",
                    L"Myanglish IME Settings",
                    MB_OK | MB_ICONERROR
                );
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    constexpr wchar_t kClassName[] = L"MyanglishSettingsWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kClassName,
        L"Myanglish IME Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 330,
        nullptr, nullptr, instance, nullptr
    );

    if (window == nullptr) {
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
