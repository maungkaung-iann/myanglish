#include <Windows.h>
#include <Shellapi.h>
#include <urlmon.h>
#include <bcrypt.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"MyanglishStoreLauncherWindow";
constexpr int kDownloadButtonId = 2001;
constexpr int kInstallButtonId = 2002;
constexpr UINT kDownloadFinishedMessage = WM_APP + 1;
constexpr UINT kDownloadFailedMessage = WM_APP + 2;

constexpr wchar_t kDefaultDownloadUrl[] =
    L"https://github.com/maungkaung-iann/myanglish/releases/download/v1.0.9/"
    L"MyanglishInstaller.exe";

constexpr wchar_t kExpectedSha256[] =
    L"33F8694067B915030251DCDBBC0303D3E04D2A391F8FFC3F2E3A44418251AF9E";

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
           L"Myanglish" /
           L"StoreLauncher";
}

std::filesystem::path installerPath() {
    return localAppDataRoot() / L"MyanglishInstaller.exe";
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

std::wstring bytesToHex(
    const std::vector<unsigned char>& bytes
) {
    std::wstringstream stream;
    stream << std::uppercase << std::hex << std::setfill(L'0');

    for (unsigned char byte : bytes) {
        stream << std::setw(2)
               << static_cast<unsigned int>(byte);
    }

    return stream.str();
}

bool calculateSha256(
    const std::filesystem::path& path,
    std::wstring& result
) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    DWORD hashObjectSize = 0;
    DWORD hashSize = 0;
    DWORD bytesWritten = 0;

    if (BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0
        ) != 0) {
        return false;
    }

    if (BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&hashObjectSize),
            sizeof(hashObjectSize),
            &bytesWritten,
            0
        ) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    if (BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize),
            sizeof(hashSize),
            &bytesWritten,
            0
        ) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<unsigned char> hashObject(hashObjectSize);
    std::vector<unsigned char> hashBytes(hashSize);

    if (BCryptCreateHash(
            algorithm,
            &hash,
            hashObject.data(),
            static_cast<ULONG>(hashObject.size()),
            nullptr,
            0,
            0
        ) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::ifstream file(path, std::ios::binary);

    if (!file.is_open()) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<char> buffer(64 * 1024);

    while (file.good()) {
        file.read(
            buffer.data(),
            static_cast<std::streamsize>(buffer.size())
        );

        const std::streamsize count = file.gcount();

        if (count > 0) {
            if (BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(count),
                    0
                ) != 0) {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                return false;
            }
        }
    }

    if (BCryptFinishHash(
            hash,
            hashBytes.data(),
            static_cast<ULONG>(hashBytes.size()),
            0
        ) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    result = bytesToHex(hashBytes);
    return true;
}

bool verifyInstallerHash() {
    const auto installer = installerPath();

    if (!std::filesystem::exists(installer)) {
        return false;
    }

    std::wstring actualHash;

    if (!calculateSha256(installer, actualHash)) {
        return false;
    }

    return _wcsicmp(
        actualHash.c_str(),
        kExpectedSha256
    ) == 0;
}

void setStatus(const wchar_t* text) {
    if (g_status != nullptr) {
        SetWindowTextW(g_status, text);
    }
}

void beginDownload(HWND window) {
    EnableWindow(g_downloadButton, FALSE);
    EnableWindow(g_installButton, FALSE);

    setStatus(
        L"Downloading Myanglish... Please keep this app open."
    );

    std::thread([window]() {
        const auto root = localAppDataRoot();

        if (root.empty()) {
            PostMessageW(
                window,
                kDownloadFailedMessage,
                0,
                0
            );
            return;
        }

        std::error_code ec;

        std::filesystem::create_directories(
            root,
            ec
        );

        if (ec) {
            PostMessageW(
                window,
                kDownloadFailedMessage,
                0,
                0
            );
            return;
        }

        const auto installer = installerPath();

        std::filesystem::remove(
            installer,
            ec
        );

        const std::wstring url = downloadUrl();

        const HRESULT hr = URLDownloadToFileW(
            nullptr,
            url.c_str(),
            installer.c_str(),
            0,
            nullptr
        );

        if (FAILED(hr) || !verifyInstallerHash()) {
            std::filesystem::remove(
                installer,
                ec
            );

            PostMessageW(
                window,
                kDownloadFailedMessage,
                0,
                0
            );

            return;
        }

        PostMessageW(
            window,
            kDownloadFinishedMessage,
            0,
            0
        );
    }).detach();
}

void launchInstaller(HWND window) {
    const auto installer = installerPath();

    if (!std::filesystem::exists(installer)) {
        MessageBoxW(
            window,
            L"Myanglish has not been downloaded yet.\n\n"
            L"Please click Download first.",
            L"Myanglish",
            MB_OK | MB_ICONWARNING
        );

        return;
    }

    if (!verifyInstallerHash()) {
        MessageBoxW(
            window,
            L"The Myanglish installer could not be verified.\n\n"
            L"Please download it again.",
            L"Myanglish",
            MB_OK | MB_ICONERROR
        );

        EnableWindow(
            g_installButton,
            FALSE
        );

        return;
    }

    HINSTANCE result = ShellExecuteW(
        window,
        L"runas",
        installer.c_str(),
        L"/silent",
        installer.parent_path().c_str(),
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(
            window,
            L"Myanglish Installer could not be started.\n\n"
            L"If you cancelled the UAC prompt, click Install again.",
            L"Myanglish",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    setStatus(
        L"Installer started. Approve the Windows UAC prompt "
        L"to finish installation."
    );
}

LRESULT CALLBACK WindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(
            L"STATIC",
            L"Myanglish",
            WS_CHILD | WS_VISIBLE,
            28, 24, 440, 32,
            window,
            nullptr,
            nullptr,
            nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"Myanmar typing without memorizing a Myanmar keyboard layout.",
            WS_CHILD | WS_VISIBLE,
            28, 60, 470, 24,
            window,
            nullptr,
            nullptr,
            nullptr
        );

        g_downloadButton = CreateWindowW(
            L"BUTTON",
            L"1. Download Myanglish",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            28, 108, 220, 44,
            window,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    kDownloadButtonId
                )
            ),
            nullptr,
            nullptr
        );

        g_installButton = CreateWindowW(
            L"BUTTON",
            L"2. Install Myanglish",
            WS_CHILD | WS_VISIBLE,
            268, 108, 220, 44,
            window,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    kInstallButtonId
                )
            ),
            nullptr,
            nullptr
        );

        g_status = CreateWindowW(
            L"STATIC",
            verifyInstallerHash()
                ? L"Download ready and verified. Click Install Myanglish."
                : L"Step 1: Download the Myanglish installer.",
            WS_CHILD | WS_VISIBLE,
            28, 176, 470, 48,
            window,
            nullptr,
            nullptr,
            nullptr
        );

        EnableWindow(
            g_installButton,
            verifyInstallerHash()
                ? TRUE
                : FALSE
        );

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
        setStatus(
            L"Download complete and verified. "
            L"Click Install Myanglish."
        );

        EnableWindow(
            g_downloadButton,
            TRUE
        );

        EnableWindow(
            g_installButton,
            TRUE
        );

        return 0;

    case kDownloadFailedMessage:
        setStatus(
            L"Download or verification failed. "
            L"Check your internet connection and try again."
        );

        EnableWindow(
            g_downloadButton,
            TRUE
        );

        EnableWindow(
            g_installButton,
            FALSE
        );

        MessageBoxW(
            window,
            L"Myanglish could not be downloaded or verified.",
            L"Myanglish",
            MB_OK | MB_ICONERROR
        );

        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        window,
        message,
        wParam,
        lParam
    );
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand
) {
    WNDCLASSW wc{};

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(
        nullptr,
        IDC_ARROW
    );

    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1
        );

    if (!RegisterClassW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        L"Myanglish",
        WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        540,
        290,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (window == nullptr) {
        return 1;
    }

    ShowWindow(
        window,
        showCommand
    );

    UpdateWindow(window);

    MSG msg{};

    while (GetMessageW(
        &msg,
        nullptr,
        0,
        0
    ) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(
        msg.wParam
    );
}
