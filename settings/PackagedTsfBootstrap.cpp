#include "PackagedTsfBootstrap.h"

#include "Guids.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <Windows.h>
#include <Objbase.h>
#include <msctf.h>

namespace myanglish::settings {
namespace {

constexpr LANGID kMyanglishLangId = 0x0455; // my-MM
constexpr wchar_t kProfileDescription[] = L"Myanglish IME";

std::filesystem::path localAppDataRoot() {
    wchar_t buffer[32768]{};
    const DWORD size = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer))
    );
    if (size > 0 && size < std::size(buffer)) {
        return std::filesystem::path(buffer) / L"MyanglishIME";
    }
    return std::filesystem::temp_directory_path() / L"MyanglishIME";
}

void logStep(const wchar_t* step, HRESULT hr) {
    try {
        const auto root = localAppDataRoot();
        std::error_code ec;
        std::filesystem::create_directories(root, ec);

        std::wofstream file(root / L"store-tsf-bootstrap.log", std::ios::app);
        if (!file.is_open()) {
            return;
        }

        file << step
             << L" hr=0x"
             << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
             << static_cast<unsigned long>(hr)
             << L"\n";
    } catch (...) {
        // Logging must never break IME setup.
    }
}

std::wstring currentModulePath() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(
        nullptr, path, static_cast<DWORD>(std::size(path))
    );
    if (length == 0 || length >= std::size(path)) {
        return {};
    }
    return std::wstring(path, length);
}

HRESULT registerProfile() {
    ITfInputProcessorProfileMgr* profileManager = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&profileManager)
    );
    logStep(L"ProfileMgr.CoCreateInstance", hr);
    if (FAILED(hr)) {
        return hr;
    }

    TF_INPUTPROCESSORPROFILE existing{};
    const HRESULT existingHr = profileManager->GetProfile(
        TF_PROFILETYPE_INPUTPROCESSOR,
        kMyanglishLangId,
        myanglish::ime::CLSID_MyanglishIME,
        myanglish::ime::GUID_MyanglishIMEProfile,
        nullptr,
        &existing
    );
    logStep(L"ProfileMgr.GetProfile", existingHr);

    if (SUCCEEDED(existingHr)) {
        profileManager->Release();
        return S_OK;
    }

    const std::wstring iconPath = currentModulePath();

    // No substitute HKL is used. Myanglish is a TSF text service, not a US
    // keyboard layout. InstallLayoutOrTip below enables it for this user.
    hr = profileManager->RegisterProfile(
        myanglish::ime::CLSID_MyanglishIME,
        kMyanglishLangId,
        myanglish::ime::GUID_MyanglishIMEProfile,
        kProfileDescription,
        static_cast<ULONG>(std::size(kProfileDescription) - 1),
        iconPath.empty() ? nullptr : iconPath.c_str(),
        static_cast<ULONG>(iconPath.size()),
        0,
        nullptr,
        0,
        FALSE,
        0
    );
    logStep(L"ProfileMgr.RegisterProfile", hr);

    profileManager->Release();
    return hr;
}

HRESULT registerCategories() {
    ITfCategoryMgr* categoryManager = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_CategoryMgr,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr,
        reinterpret_cast<void**>(&categoryManager)
    );
    logStep(L"CategoryMgr.CoCreateInstance", hr);
    if (FAILED(hr)) {
        return hr;
    }

    const GUID categories[] = {
        GUID_TFCAT_TIP_KEYBOARD,
        GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
        GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    };

    for (const GUID& category : categories) {
        hr = categoryManager->RegisterCategory(
            myanglish::ime::CLSID_MyanglishIME,
            category,
            myanglish::ime::CLSID_MyanglishIME
        );
        logStep(L"CategoryMgr.RegisterCategory", hr);
        if (FAILED(hr)) {
            categoryManager->Release();
            return hr;
        }
    }

    categoryManager->Release();
    return S_OK;
}

HRESULT installLayoutOrTip() {
    HMODULE inputDll = LoadLibraryExW(
        L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32
    );
    if (inputDll == nullptr) {
        const DWORD error = GetLastError();
        const HRESULT hr = HRESULT_FROM_WIN32(
            error == ERROR_SUCCESS ? ERROR_MOD_NOT_FOUND : error
        );
        logStep(L"LoadLibraryEx(input.dll)", hr);
        return hr;
    }

    using InstallLayoutOrTipFn = BOOL (WINAPI*)(LPCWSTR, DWORD);
    const auto install = reinterpret_cast<InstallLayoutOrTipFn>(
        GetProcAddress(inputDll, "InstallLayoutOrTip")
    );

    if (install == nullptr) {
        const DWORD error = GetLastError();
        FreeLibrary(inputDll);
        const HRESULT hr = HRESULT_FROM_WIN32(
            error == ERROR_SUCCESS ? ERROR_PROC_NOT_FOUND : error
        );
        logStep(L"GetProcAddress(InstallLayoutOrTip)", hr);
        return hr;
    }

    // Microsoft TSF profile string format:
    // <LangID>:{CLSID of TIP}{GUID of language profile}
    constexpr wchar_t profile[] =
        L"0x0455:"
        L"{5F8C4D13-9E7E-4E5B-8C38-123159872A10}"
        L"{B6D0AF10-6A43-4D0C-947D-9A4DBEF2B851}";

    const BOOL ok = install(profile, 0);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    FreeLibrary(inputDll);

    const HRESULT hr = ok
        ? S_OK
        : HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
    logStep(L"InstallLayoutOrTip", hr);
    return hr;
}

} // namespace

PackagedTsfBootstrapResult ensurePackagedTsfRegistration() {
    PackagedTsfBootstrapResult result{};

    bool shouldUninitialize = false;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        shouldUninitialize = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        logStep(L"CoInitializeEx", hr);
        return {hr, L"CoInitializeEx"};
    }
    logStep(L"CoInitializeEx", S_OK);

    hr = registerProfile();
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {hr, L"RegisterProfile"};
    }

    hr = registerCategories();
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {hr, L"RegisterCategories"};
    }

    hr = installLayoutOrTip();
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {hr, L"InstallLayoutOrTip"};
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }

    return {S_OK, L"ready"};
}

} // namespace myanglish::settings
