#include <Windows.h>
#include <Shellapi.h>
#include "Resource.h"

#include <filesystem>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"MyanglishSetupWindow";
constexpr int kInstallButtonId = 1001;
constexpr int kUninstallButtonId = 1002;
constexpr wchar_t kProductDirectory[] = L"Myanglish";
constexpr wchar_t kInstallVersion[] = L"1.0.10";

std::filesystem::path modulePath() {
    wchar_t path[32768]{};
    const DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (len == 0 || len >= std::size(path)) {
        return {};
    }
    return std::filesystem::path(std::wstring(path, len));
}

std::filesystem::path moduleDirectory() {
    const auto path = modulePath();
    return path.empty() ? std::filesystem::path{} : path.parent_path();
}

std::filesystem::path installDirectory() {
    wchar_t path[32768]{};
    const DWORD len = GetEnvironmentVariableW(
        L"ProgramFiles",
        path,
        static_cast<DWORD>(std::size(path))
    );

    if (len == 0 || len >= std::size(path)) {
        return {};
    }

    return std::filesystem::path(std::wstring(path, len))
        / kProductDirectory
        / L"versions"
        / kInstallVersion;
}

std::filesystem::path setupLogPath() {
    wchar_t path[32768]{};
    const DWORD len = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        path,
        static_cast<DWORD>(std::size(path))
    );

    std::filesystem::path root;
    if (len > 0 && len < std::size(path)) {
        root = std::filesystem::path(path) / L"MyanglishIME";
    } else {
        root = moduleDirectory();
    }

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root / L"setup.log";
}

void logSetupAction(const char* action, HRESULT hr) {
    try {
        std::ofstream file(setupLogPath(), std::ios::app);
        if (!file.is_open()) {
            return;
        }
        file << action << " hr=0x"
             << std::hex << std::uppercase
             << static_cast<unsigned long>(hr)
             << "\n";
    } catch (...) {
    }
}

HRESULT writeResourceToFile(
    int resourceId,
    const std::filesystem::path& destination
) {
    HRSRC resource = FindResourceW(
        nullptr,
        MAKEINTRESOURCEW(resourceId),
        RT_RCDATA
    );

    if (resource == nullptr) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HGLOBAL loaded = LoadResource(nullptr, resource);

    if (loaded == nullptr) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const DWORD size = SizeofResource(nullptr, resource);
    const void* data = LockResource(loaded);

    if (data == nullptr || size == 0) {
        return E_FAIL;
    }

    /*
        Same-version reinstall fix.

        If the destination file already exists and contains
        exactly the same bytes as the embedded installer file,
        leave it alone.

        This is important for MyanglishIME.dll because Explorer,
        Chrome, Edge, and other TSF clients may currently have
        the DLL loaded.
    */
    {
        std::error_code existsError;

        if (std::filesystem::exists(destination, existsError) &&
            !existsError) {

            std::error_code sizeError;
            const auto existingSize =
                std::filesystem::file_size(destination, sizeError);

            if (!sizeError &&
                existingSize == static_cast<std::uintmax_t>(size)) {

                std::ifstream existing(
                    destination,
                    std::ios::binary
                );

                if (existing.is_open()) {
                    constexpr std::size_t kBufferSize = 64 * 1024;

                    std::vector<char> buffer(kBufferSize);

                    const auto* resourceBytes =
                        static_cast<const unsigned char*>(data);

                    std::uintmax_t offset = 0;
                    bool identical = true;

                    while (
                        identical &&
                        offset < static_cast<std::uintmax_t>(size)
                    ) {
                        const std::uintmax_t remaining =
                            static_cast<std::uintmax_t>(size) - offset;

                        const std::size_t chunk =
                            static_cast<std::size_t>(
                                (remaining < kBufferSize)
                                    ? remaining
                                    : kBufferSize
                            );

                        existing.read(
                            buffer.data(),
                            static_cast<std::streamsize>(chunk)
                        );

                        if (
                            existing.gcount() !=
                            static_cast<std::streamsize>(chunk)
                        ) {
                            identical = false;
                            break;
                        }

                        if (
                            std::memcmp(
                                buffer.data(),
                                resourceBytes + offset,
                                chunk
                            ) != 0
                        ) {
                            identical = false;
                            break;
                        }

                        offset += chunk;
                    }

                    if (identical) {
                        return S_OK;
                    }
                }
            }
        }
    }

    std::error_code ec;

    std::filesystem::create_directories(
        destination.parent_path(),
        ec
    );

    if (ec) {
        return HRESULT_FROM_WIN32(ec.value());
    }

    std::ofstream file(
        destination,
        std::ios::binary | std::ios::trunc
    );

    if (!file.is_open()) {
        return HRESULT_FROM_WIN32(
            ERROR_SHARING_VIOLATION
        );
    }

    file.write(
        static_cast<const char*>(data),
        static_cast<std::streamsize>(size)
    );

    if (!file.good()) {
        return E_FAIL;
    }

    return S_OK;
}
struct EmbeddedFile {
    int resourceId;
    const wchar_t* relativePath;
};

HRESULT copyPayloadToInstallDirectory() {
    const auto destination = installDirectory();

    if (destination.empty()) {
        return E_FAIL;
    }

    const EmbeddedFile files[] = {
        { IDR_MYANGLISH_IME, L"MyanglishIME.dll" },
        { IDR_MYANGLISH_SETTINGS, L"MyanglishSettings.exe" },

        { IDR_DATA_1000, L"data\\alpha08_candidates.csv" },
        { IDR_DATA_1001, L"data\\dictionary.csv" },
        { IDR_DATA_1002, L"data\\historical_candidates.csv" },
        { IDR_DATA_1003, L"data\\imported_lexicon_alpha1083_pack1.csv" },
        { IDR_DATA_1004, L"data\\imported_lexicon_alpha1083_pack2.csv" },

        { IDR_DATA_1005, L"data\\lexicon\\burmese_lexicon.txt" },
        { IDR_DATA_1006, L"data\\lexicon\\README.md" },

        { IDR_DATA_1007, L"data\\myanglish_rules\\ranked_suffixes_alpha07.csv" },
        { IDR_DATA_1008, L"data\\myanglish_rules\\ranked_suffixes_alpha08.csv" },
        { IDR_DATA_1009, L"data\\myanglish_rules\\rhymes_master_v1.csv" },
        { IDR_DATA_1010, L"data\\myanglish_rules\\suffix_families_alpha08.csv" },

        { IDR_DATA_1011, L"data\\ranked_candidates_alpha0109.csv" },

        { IDR_DATA_1012, L"data\\rules\\finals.csv" },
        { IDR_DATA_1013, L"data\\rules\\marks.csv" },
        { IDR_DATA_1014, L"data\\rules\\medials.csv" },
        { IDR_DATA_1015, L"data\\rules\\rhymes.csv" },
        { IDR_DATA_1016, L"data\\rules\\tone_marks.csv" },

        { IDR_DATA_1017, L"data\\user_dictionary.example.csv" },
        { IDR_DATA_1018, L"data\\vowel.csv" }
    };

    for (const auto& file : files) {
        const HRESULT hr = writeResourceToFile(
            file.resourceId,
            destination / file.relativePath
        );

        if (FAILED(hr)) {
            return hr;
        }
    }

    const auto installedDll = destination / L"MyanglishIME.dll";

    std::error_code ec;
    if (!std::filesystem::exists(installedDll, ec) || ec) {
        return HRESULT_FROM_WIN32(
            ec ? ec.value() : ERROR_FILE_NOT_FOUND
        );
    }

    return S_OK;
}
HRESULT callRegistrationExportForDll(const std::filesystem::path& dllPath, bool uninstall) {
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr) {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        logSetupAction(uninstall ? "Uninstall.LoadLibrary" : "Install.LoadLibrary", hr);
        return hr;
    }

    using RegisterFn = HRESULT (STDAPICALLTYPE*)(void);
    const char* exportName = uninstall ? "DllUnregisterServer" : "DllRegisterServer";
    auto fn = reinterpret_cast<RegisterFn>(GetProcAddress(module, exportName));
    if (fn == nullptr) {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        FreeLibrary(module);
        logSetupAction(uninstall ? "Uninstall.GetProcAddress" : "Install.GetProcAddress", hr);
        return hr;
    }

    const HRESULT hr = fn();

    // Keep the module loaded through install because that is the configuration
    // that passed the elevated TSF registration test. On uninstall, release it
    // immediately after DllUnregisterServer so cleanup can remove the DLL.
    if (uninstall) {
        FreeLibrary(module);
    }

    logSetupAction(uninstall ? "Uninstall" : "Install", hr);

    // ctfmon refresh is useful after installation. Do not launch it during
    // uninstall because another process loading the DLL can delay file cleanup.
    if (SUCCEEDED(hr) && !uninstall) {
        wchar_t ctfmonPath[MAX_PATH]{};
        GetSystemDirectoryW(ctfmonPath, MAX_PATH);
        std::filesystem::path ctf = std::filesystem::path(ctfmonPath) / L"ctfmon.exe";
        ShellExecuteW(nullptr, L"open", ctf.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    return hr;
}

HRESULT installMyanglish() {
    HRESULT hr = copyPayloadToInstallDirectory();
    logSetupAction("Install.CopyPayload", hr);
    if (FAILED(hr)) {
        return hr;
    }

    const auto dllPath = installDirectory() / L"MyanglishIME.dll";
    return callRegistrationExportForDll(dllPath, false);
}

std::wstring quoteArg(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

HRESULT startDeferredCleanup(const std::filesystem::path& destination) {
    if (destination.empty()) {
        return E_INVALIDARG;
    }

    wchar_t tempPath[MAX_PATH]{};
    const DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const DWORD pid = GetCurrentProcessId();
    const auto helperPath = std::filesystem::path(tempPath) /
        (L"MyanglishSetupCleanup-" + std::to_wstring(pid) + L".exe");

    std::error_code ec;
    std::filesystem::copy_file(
        modulePath(),
        helperPath,
        std::filesystem::copy_options::overwrite_existing,
        ec
    );
    if (ec) {
        return HRESULT_FROM_WIN32(ec.value());
    }

    std::wstring commandLine = quoteArg(helperPath.wstring()) +
        L" /cleanup " + std::to_wstring(pid) + L" " + quoteArg(destination.wstring());
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(
            helperPath.c_str(),
            buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi)) {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        std::filesystem::remove(helperPath, ec);
        return hr;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return S_OK;
}

HRESULT uninstallMyanglish() {
    const auto permanentDll = installDirectory() / L"MyanglishIME.dll";
    const auto fallbackDll = moduleDirectory() / L"MyanglishIME.dll";

    std::error_code ec;
    const auto dllPath = std::filesystem::exists(permanentDll, ec) && !ec
        ? permanentDll
        : fallbackDll;

    HRESULT hr = callRegistrationExportForDll(dllPath, true);
    if (FAILED(hr)) {
        return hr;
    }

    // Remove the whole Myanglish installation tree after Setup exits.
    // This includes older version directories left behind by safe upgrades.
    const auto versionDirectory = installDirectory();
    const auto versionsDirectory = versionDirectory.parent_path();
    const auto destination = versionsDirectory.parent_path();

    if (destination.empty() || destination.filename() != kProductDirectory) {
        return E_FAIL;
    }

    hr = startDeferredCleanup(destination);
    logSetupAction("Uninstall.ScheduleCleanup", hr);
    return hr;
}

int runCleanupHelper(DWORD parentPid, const std::filesystem::path& destination) {
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (parent != nullptr) {
        WaitForSingleObject(parent, 30000);
        CloseHandle(parent);
    } else {
        Sleep(1000);
    }

    // Retry for up to 30 seconds because TSF/text-input processes can keep the
    // DLL mapped for a short time after profile removal.
    std::error_code ec;
    bool removed = false;
    for (int attempt = 0; attempt < 60; ++attempt) {
        ec.clear();
        std::filesystem::remove_all(destination, ec);
        if (!ec) {
            removed = true;
            break;
        }
        Sleep(500);
    }

    HRESULT hr = removed ? S_OK : HRESULT_FROM_WIN32(ec.value());

    // Do not force-close the user's applications. If an old IME DLL is still
    // mapped, schedule the remaining installation files for deletion at reboot.
    if (!removed) {
        std::error_code walkEc;
        std::vector<std::filesystem::path> directories;

        if (std::filesystem::exists(destination, walkEc) && !walkEc) {
            for (std::filesystem::recursive_directory_iterator it(
                     destination,
                     std::filesystem::directory_options::skip_permission_denied,
                     walkEc),
                 end;
                 it != end;
                 it.increment(walkEc)) {
                if (walkEc) {
                    walkEc.clear();
                    continue;
                }

                walkEc.clear();
                if (it->is_directory(walkEc) && !walkEc) {
                    directories.push_back(it->path());
                } else if (!walkEc) {
                    MoveFileExW(
                        it->path().c_str(),
                        nullptr,
                        MOVEFILE_DELAY_UNTIL_REBOOT
                    );
                }
            }

            for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
                MoveFileExW(
                    it->c_str(),
                    nullptr,
                    MOVEFILE_DELAY_UNTIL_REBOOT
                );
            }

            MoveFileExW(
                destination.c_str(),
                nullptr,
                MOVEFILE_DELAY_UNTIL_REBOOT
            );

            hr = S_OK;
            logSetupAction("Uninstall.RebootCleanupScheduled", S_OK);
        }
    }

    logSetupAction("Uninstall.DeferredCleanup", hr);

    // The helper cannot delete its own executable while running.
    const auto self = modulePath();
    if (!self.empty()) {
        MoveFileExW(self.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    return SUCCEEDED(hr) ? 0 : 1;
}
HRESULT performSetupAction(bool uninstall) {
    return uninstall ? uninstallMyanglish() : installMyanglish();
}

void showResult(HWND owner, bool uninstall, HRESULT hr) {
    wchar_t message[512]{};
    if (SUCCEEDED(hr)) {
        swprintf_s(
            message,
            uninstall
                ? L"Myanglish was uninstalled successfully.\n\nInstalled files will be removed automatically.\nHRESULT: 0x%08lX"
                : L"Myanglish was installed successfully.\n\nInstalled to C:\\Program Files\\Myanglish\nUse Win + Space to select Myanglish.\nHRESULT: 0x%08lX",
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
            L"Remove Myanglish",
            WS_CHILD | WS_VISIBLE,
            238, 110, 150, 42,
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
        if (HIWORD(wParam) != BN_CLICKED) {
            break;
        }

        if (LOWORD(wParam) == kInstallButtonId) {
            const HRESULT hr = performSetupAction(false);
            showResult(window, false, hr);
            return 0;
        }

        if (LOWORD(wParam) == kUninstallButtonId) {
            const int answer = MessageBoxW(
                window,
                L"Remove Myanglish from Windows?",
                L"Myanglish Setup",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
            );
            if (answer != IDYES) {
                return 0;
            }

            const HRESULT hr = performSetupAction(true);
            showResult(window, true, hr);
            if (SUCCEEDED(hr)) {
                DestroyWindow(window);
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
        540, 270,
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

    if (args.rfind(L"/cleanup ", 0) == 0) {
        const size_t firstSpace = args.find(L' ');
        const size_t secondSpace = args.find(L' ', firstSpace + 1);
        if (firstSpace == std::wstring::npos || secondSpace == std::wstring::npos) {
            return 1;
        }

        const std::wstring pidText = args.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        DWORD parentPid = 0;
        try {
            parentPid = static_cast<DWORD>(std::stoul(pidText));
        } catch (...) {
            return 1;
        }

        std::wstring target = args.substr(secondSpace + 1);
        if (target.size() >= 2 && target.front() == L'\"' && target.back() == L'\"') {
            target = target.substr(1, target.size() - 2);
        }
        return runCleanupHelper(parentPid, std::filesystem::path(target));
    }

    if (args.find(L"/silent") != std::wstring::npos) {
        const bool uninstall = args.find(L"/uninstall") != std::wstring::npos;
        const HRESULT hr = performSetupAction(uninstall);
        return FAILED(hr) ? 1 : 0;
    }

    return runInteractive(instance, showCommand);
}

