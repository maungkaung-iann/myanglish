#include <Windows.h>

#include <filesystem>
#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"MyanglishSetupWindow";
constexpr int kInstallButtonId = 1001;
constexpr int kUninstallButtonId = 1002;

std::filesystem::path moduleDirectory() {
    wchar_t path[32768]{};
    const DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (len == 0 || len >= std::size(path)) {
        return {};
    }
    return std::filesystem::path(std::wstring(path, len)).parent_path();
}

HRESULT callRegistrationExport(bool uninstall) {
    const auto dllPath = moduleDirectory() / L"MyanglishIME.dll";
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    using RegisterFn = HRESULT (STDAPICALLTYPE*)(void);
    const char* exportName = uninstall ? "DllUnregisterServer" : "DllRegisterServer";
    auto fn = reinterpret_cast<RegisterFn>(GetProcAddress(module, exportName));
    if (fn == nullptr) {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        FreeLibrary(module);
        return hr;
    }

    const HRESULT hr = fn();
    FreeLibrary(module);

    if (SUCCEEDED(hr)) {
        wchar_t ctfmonPath[MAX_PATH]{};
        GetSystemDirectoryW(ctfmonPath, MAX_PATH);
        std::filesystem::path ctf = std::filesystem::path(ctfmonPath) / L"ctfmon.exe";
        ShellExecuteW(nullptr, L"open", ctf.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    return hr;
}

void showResult(HWND owner, bool uninstall, HRESULT hr) {
    wchar_t message[512]{};
    if (SUCCEEDED(hr)) {
        swprintf_s(
            message,
            uninstall
                ? L"Myanglish was uninstalled successfully.\n\nHRESULT: 0x%08lX"
                : L"Myanglish was installed successfully.\n\nUse Win + Space to select Myanglish.\nHRESULT: 0x%08lX",
            static_cast<unsigned long>(hr)
        );
        MessageBoxW(owner, message, L"Myanglish Setup", MB_OK | MB_ICONINFORMATION);
    } else {
        swprintf_s(
            message,
            uninstall
                ? L"Myanglish uninstall failed.\n\nHRESULT: 0x%08lX"
                : L"Myanglish install failed.\n\nHRESULT: 0x%08lX",
            static_cast<unsigned long>(hr)
        );
        MessageBoxW(owner, message, L"Myanglish Setup", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(
            L"STATIC",
            L"Welcome to Myanglish Setup",
            WS_CHILD | WS_VISIBLE,
            28, 24, 360, 28,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"Install Myanglish as a Windows TSF input method.",
            WS_CHILD | WS_VISIBLE,
            28, 62, 430, 24,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowW(
            L"BUTTON",
            L"Install Myanglish",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            28, 110, 190, 42,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInstallButtonId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"BUTTON",
            L"Uninstall",
            WS_CHILD | WS_VISIBLE,
            238, 110, 120, 42,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUninstallButtonId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"Administrator permission is required for TSF registration.",
            WS_CHILD | WS_VISIBLE,
            28, 174, 430, 24,
            window, nullptr, nullptr, nullptr
        );
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == kInstallButtonId) {
            const HRESULT hr = callRegistrationExport(false);
            showResult(window, false, hr);
            return 0;
        }
        if (LOWORD(wParam) == kUninstallButtonId) {
            const HRESULT hr = callRegistrationExport(true);
            showResult(window, true, hr);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int runInteractive(HINSTANCE instance, int showCommand) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        L"Myanglish Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        520, 260,
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

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    const std::wstring args = commandLine == nullptr ? L"" : commandLine;

    if (args.find(L"/silent") != std::wstring::npos) {
        const bool uninstall = args.find(L"/uninstall") != std::wstring::npos;
        const HRESULT hr = callRegistrationExport(uninstall);
        return FAILED(hr) ? 1 : 0;
    }

    return runInteractive(instance, showCommand);
}
