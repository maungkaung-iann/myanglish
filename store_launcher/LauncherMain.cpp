#include <Windows.h>
#include <Shellapi.h>
#include <urlmon.h>

#include <filesystem>
#include <string>
#include <thread>

#pragma comment(lib, "urlmon.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"MyanglishStoreLauncherWindow";
constexpr int kDownloadButtonId = 2001;
constexpr int kInstallButtonId = 2002;
constexpr UINT kDownloadFinishedMessage = WM_APP + 1;
constexpr UINT kDownloadFailedMessage = WM_APP + 2;

// Replace this with an immutable/versioned HTTPS release asset before Store submission.
constexpr wchar_t kDefaultDownloadUrl[] =
    L"https://github.com/maungkaung-iann/myanglish/releases/download/v1.0.3/"
    L"Myanglish-Installer-Payload-v1.0.3.zip";

HWND g_status = nullptr;
HWND g_downloadButton = nullptr;
HWND g_installButton = nullptr;

std::filesystem::path localAppDataRoot() {
    wchar_t path[32768]{};
    const DWORD len = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        path,
        static_cast<DWORD>(std::size(path))
    );

    if (len == 0 || len >= std::size(path)) {
        return {};
    }

    return std::filesystem::path(std::wstring(path, len)) /
           L"Myanglish" / L"StoreLauncher";
}

std::filesystem::path zipPath() {
    return localAppDataRoot() / L"Myanglish-Installer-Payload.zip";
}

std::filesystem::path extractDirectory() {
    return localAppDataRoot() / L"payload";
}

std::filesystem::path setupPath() {
    return extractDirectory() / L"MyanglishInstaller.exe";
}

std::wstring downloadUrl() {
    wchar_t value[32768]{};
    const DWORD len = GetEnvironmentVariableW(
        L"MYANGLISH_DOWNLOAD_URL",
        value,
        static_cast<DWORD>(std::size(value))
    );

    if (len > 0 && len < std::size(value)) {
        return std::wstring(value, len);
    }

    return kDefaultDownloadUrl;
}

std::wstring powerShellQuote(const std::filesystem::path& path) {
    std::wstring value = path.wstring();
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        if (ch == L'\'') {
            escaped += L"''";
        } else {
            escaped += ch;
        }
    }
    return L"'" + escaped + L"'";
}

bool extractZip() {
    const auto archive = zipPath();
    const auto destination = extractDirectory();

    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
    ec.clear();
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        return false;
    }

    const std::wstring command =
        L"powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass "
        L"-Command \"Expand-Archive -LiteralPath " + powerShellQuote(archive) +
        L" -DestinationPath " + powerShellQuote(destination) + L" -Force\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring mutableCommand = command;
    if (!CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return exitCode == 0 && std::filesystem::exists(setupPath());
}

void setStatus(const wchar_t* text) {
    if (g_status != nullptr) {
        SetWindowTextW(g_status, text);
    }
}

void beginDownload(HWND window) {
    EnableWindow(g_downloadButton, FALSE);
    EnableWindow(g_installButton, FALSE);
    setStatus(L"Downloading Myanglish... Please keep this app open.");

    std::thread([window]() {
        const auto root = localAppDataRoot();
        if (root.empty()) {
            PostMessageW(window, kDownloadFailedMessage, 0, 0);
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        if (ec) {
            PostMessageW(window, kDownloadFailedMessage, 0, 0);
            return;
        }

        const std::wstring url = downloadUrl();
        const HRESULT hr = URLDownloadToFileW(
            nullptr,
            url.c_str(),
            zipPath().c_str(),
            0,
            nullptr
        );

        if (FAILED(hr) || !extractZip()) {
            PostMessageW(window, kDownloadFailedMessage, 0, 0);
            return;
        }

        PostMessageW(window, kDownloadFinishedMessage, 0, 0);
    }).detach();
}

void launchInstaller(HWND window) {
    const auto setup = setupPath();
    if (!std::filesystem::exists(setup)) {
        MessageBoxW(
            window,
            L"Myanglish has not been downloaded yet.\n\nPlease click Download first.",
            L"Myanglish",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    // The setup EXE itself also requests requireAdministrator. Using runas here
    // makes the intended elevation explicit. Windows will show the UAC prompt;
    // the user must approve it.
    HINSTANCE result = ShellExecuteW(
        window,
        L"runas",
        setup.c_str(),
        L"/silent",
        setup.parent_path().c_str(),
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(
            window,
            L"Myanglish Setup could not be started.\n\nIf you cancelled the UAC prompt, click Install again.",
            L"Myanglish",
            MB_OK | MB_ICONERROR
        );
        return;
    }

    setStatus(L"Installer started. Approve the Windows UAC prompt to finish installation.");
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(
            L"STATIC",
            L"Myanglish",
            WS_CHILD | WS_VISIBLE,
            28, 24, 440, 32,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"Myanmar typing without memorizing a Myanmar keyboard layout.",
            WS_CHILD | WS_VISIBLE,
            28, 60, 470, 24,
            window, nullptr, nullptr, nullptr
        );

        g_downloadButton = CreateWindowW(
            L"BUTTON",
            L"1. Download Myanglish",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            28, 108, 220, 44,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDownloadButtonId)),
            nullptr, nullptr
        );

        g_installButton = CreateWindowW(
            L"BUTTON",
            L"2. Install Myanglish",
            WS_CHILD | WS_VISIBLE,
            268, 108, 220, 44,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInstallButtonId)),
            nullptr, nullptr
        );

        g_status = CreateWindowW(
            L"STATIC",
            std::filesystem::exists(setupPath())
                ? L"Download ready. Click Install Myanglish."
                : L"Step 1: Download the Myanglish installer files.",
            WS_CHILD | WS_VISIBLE,
            28, 176, 470, 48,
            window, nullptr, nullptr, nullptr
        );

        EnableWindow(g_installButton, std::filesystem::exists(setupPath()) ? TRUE : FALSE);
        return 0;

    case WM_COMMAND:
        if (HIWORD(wParam) != BN_CLICKED) {
            break;
        }

        if (LOWORD(wParam) == kDownloadButtonId) {
            beginDownload(window);
            return 0;
        }

        if (LOWORD(wParam) == kInstallButtonId) {
            launchInstaller(window);
            return 0;
        }
        break;

    case kDownloadFinishedMessage:
        setStatus(L"Download complete. Click Install Myanglish.");
        EnableWindow(g_downloadButton, TRUE);
        EnableWindow(g_installButton, TRUE);
        return 0;

    case kDownloadFailedMessage:
        setStatus(L"Download or extraction failed. Check your internet connection and try again.");
        EnableWindow(g_downloadButton, TRUE);
        EnableWindow(g_installButton, FALSE);
        MessageBoxW(
            window,
            L"Myanglish could not be downloaded or extracted.",
            L"Myanglish",
            MB_OK | MB_ICONERROR
        );
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        L"Myanglish",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        540, 290,
        nullptr, nullptr, instance, nullptr
    );

    if (window == nullptr) {
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
