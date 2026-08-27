#include "PersonalDictionaryManager.h"
#include "Globals.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <Windows.h>

namespace myanglish::ime {
namespace {

constexpr wchar_t kClassName[] = L"MyanglishPersonalDictionaryR112";
constexpr int kRawId = 3101;
constexpr int kMyanmarId = 3102;
constexpr int kAddId = 3103;
constexpr int kListId = 3104;
constexpr int kDeleteId = 3105;
constexpr int kCloseId = 3106;

struct Word {
    std::string raw;
    std::string burmese;
    int frequency = 2000000;
};

struct State {
    std::wstring prefill;
    bool done = false;
};

std::filesystem::path dictionaryPath() {
    wchar_t buffer[32768]{};
    DWORD n = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer))
    );
    if (n > 0 && n < std::size(buffer)) {
        return std::filesystem::path(buffer)
            / L"MyanglishIME" / L"user_dictionary.csv";
    }
    return std::filesystem::current_path()
        / L"MyanglishIME" / L"user_dictionary.csv";
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr
    );
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        out.data(), n, nullptr, nullptr
    );
    return out;
}

std::wstring fromUtf8(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0
    );
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        out.data(), n
    );
    return out;
}

std::wstring getText(HWND w, int id) {
    HWND c = GetDlgItem(w, id);
    if (!c) return {};
    int n = GetWindowTextLengthW(c);
    if (n <= 0) return {};
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    int copied = GetWindowTextW(c, s.data(), n + 1);
    s.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
    return s;
}

std::vector<Word> loadWords() {
    std::vector<Word> words;
    std::ifstream file(dictionaryPath(), std::ios::binary);
    if (!file.is_open()) return words;

    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (first) {
            first = false;
            if (line.rfind("myanglish,", 0) == 0) continue;
        }

        std::stringstream ss(line);
        Word word;
        std::string freq;
        if (!std::getline(ss, word.raw, ',')) continue;
        if (!std::getline(ss, word.burmese, ',')) continue;
        if (std::getline(ss, freq) && !freq.empty()) {
            try { word.frequency = std::stoi(freq); } catch (...) {}
        }
        if (!word.raw.empty() && !word.burmese.empty()) {
            words.push_back(std::move(word));
        }
    }
    return words;
}

bool saveWords(const std::vector<Word>& words) {
    try {
        auto path = dictionaryPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        file << "myanglish,burmese,frequency\n";
        for (const auto& word : words) {
            file << word.raw << "," << word.burmese << ","
                 << word.frequency << "\n";
        }
        return file.good();
    } catch (...) {
        return false;
    }
}

void refreshList(HWND w) {
    HWND list = GetDlgItem(w, kListId);
    if (!list) return;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);

    for (const auto& word : loadWords()) {
        std::wstring line = fromUtf8(word.raw);
        line += L"   →   ";
        line += fromUtf8(word.burmese);
        SendMessageW(
            list, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(line.c_str())
        );
    }
}

HWND addChild(
    HWND parent, DWORD exStyle, const wchar_t* cls,
    const wchar_t* text, DWORD style,
    int x, int y, int width, int height, int id
) {
    HWND child = CreateWindowExW(
        exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent,
        id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
        moduleHandle(), nullptr
    );
    if (child) {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (font) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
    return child;
}

LRESULT CALLBACK windowProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam
) {
    State* state = reinterpret_cast<State*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );

    if (message == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<State*>(cs->lpCreateParams);
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state)
        );
    }

    switch (message) {
    case WM_CREATE: {
        addChild(window, 0, L"STATIC", L"Personal Dictionary",
                 SS_LEFT, 20, 16, 300, 24, 0);
        addChild(window, 0, L"STATIC", L"Myanglish",
                 SS_LEFT, 20, 58, 85, 22, 0);

        HWND raw = addChild(
            window, WS_EX_CLIENTEDGE, L"EDIT",
            state ? state->prefill.c_str() : L"",
            ES_AUTOHSCROLL, 105, 54, 205, 28, kRawId
        );

        addChild(window, 0, L"STATIC", L"Myanmar",
                 SS_LEFT, 325, 58, 80, 22, 0);
        addChild(window, WS_EX_CLIENTEDGE, L"EDIT", L"",
                 ES_AUTOHSCROLL, 405, 54, 205, 28, kMyanmarId);

        addChild(window, 0, L"BUTTON", L"Add Word",
                 BS_DEFPUSHBUTTON, 490, 96, 120, 32, kAddId);

        addChild(
            window, WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            20, 144, 590, 190, kListId
        );

        addChild(window, 0, L"BUTTON", L"Delete Selected",
                 BS_PUSHBUTTON, 20, 348, 145, 32, kDeleteId);
        addChild(window, 0, L"BUTTON", L"Close",
                 BS_PUSHBUTTON, 490, 348, 120, 32, kCloseId);

        refreshList(window);
        if (raw) SetFocus(raw);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kAddId: {
            std::wstring rawW = getText(window, kRawId);
            std::wstring burmeseW = getText(window, kMyanmarId);

            if (rawW.empty() || burmeseW.empty()
                || rawW.find(L',') != std::wstring::npos
                || burmeseW.find(L',') != std::wstring::npos) {
                MessageBoxW(
                    window, L"Enter both fields. Commas are not allowed.",
                    L"Myanglish", MB_OK | MB_ICONWARNING
                );
                return 0;
            }

            std::string raw = toUtf8(rawW);
            std::string burmese = toUtf8(burmeseW);
            auto words = loadWords();

            bool found = false;
            for (auto& word : words) {
                if (word.raw == raw && word.burmese == burmese) {
                    word.frequency = 2000000;
                    found = true;
                    break;
                }
            }
            if (!found) words.push_back(Word{raw, burmese, 2000000});

            if (saveWords(words)) {
                SetWindowTextW(GetDlgItem(window, kRawId), L"");
                SetWindowTextW(GetDlgItem(window, kMyanmarId), L"");
                refreshList(window);
                MessageBoxW(
                    window,
                    L"Word added. Switch away from Myanglish and back once to reload.",
                    L"Myanglish", MB_OK | MB_ICONINFORMATION
                );
            }
            return 0;
        }

        case kDeleteId: {
            HWND list = GetDlgItem(window, kListId);
            LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (selected != LB_ERR) {
                auto words = loadWords();
                std::size_t i = static_cast<std::size_t>(selected);
                if (i < words.size()) {
                    words.erase(words.begin() + static_cast<std::ptrdiff_t>(i));
                    saveWords(words);
                    refreshList(window);
                }
            }
            return 0;
        }

        case kCloseId:
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        if (state) state->done = true;
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool ensureWindowClass() {
    static bool attempted = false;
    static bool ok = false;
    if (attempted) return ok;
    attempted = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = moduleHandle();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;

    ATOM atom = RegisterClassExW(&wc);
    ok = atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return ok;
}

} // namespace

bool showPersonalDictionaryManager(const std::wstring& prefill) {
    if (!ensureWindowClass()) return false;

    State state;
    state.prefill = prefill;

    HWND window = CreateWindowExW(
        WS_EX_TOPMOST, kClassName,
        L"Myanglish - Add / manage words",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 650, 430,
        nullptr, nullptr, moduleHandle(), &state
    );
    if (!window) return false;

    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);

    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return true;
}

} // namespace myanglish::ime
