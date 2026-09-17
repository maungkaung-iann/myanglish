#include "Registration.h"

#include "Globals.h"
#include "Guids.h"

#include <array>
#include <string>

#include <Windows.h>
#include <Objbase.h>
#include <msctf.h>

namespace myanglish::ime {

namespace {

constexpr LANGID kMyanglishLangId = 0x0455; // Burmese (Myanmar), my-MM
constexpr const wchar_t kProfileDescription[] = L"Myanglish IME";
constexpr const wchar_t kUsKeyboardLayoutName[] = L"00000409";
constexpr const wchar_t kMyanglishTip[] =
    L"0x0455:"
    L"{5F8C4D13-9E7E-4E5B-8C38-123159872A10}"
    L"{B6D0AF10-6A43-4D0C-947D-9A4DBEF2B851}";
constexpr DWORD kIlotUninstall = 0x00000001;

std::wstring guidToString(REFGUID guid) {
    std::array<wchar_t, 64> buffer{};
    const int length = StringFromGUID2(guid, buffer.data(), static_cast<int>(buffer.size()));
    if (length <= 0) return {};
    return std::wstring(buffer.data());
}

HRESULT setRegistryStringValue(HKEY root, const std::wstring& subKey,
                               const std::wstring& valueName, const std::wstring& value) {
    HKEY key = nullptr;
    const LONG createResult = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (createResult != ERROR_SUCCESS) return HRESULT_FROM_WIN32(createResult);

    const LONG setResult = RegSetValueExW(key,
        valueName.empty() ? nullptr : valueName.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return setResult == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(setResult);
}

HRESULT registerComServer() {
    const std::wstring clsidString = guidToString(CLSID_MyanglishIME);
    if (clsidString.empty()) return E_FAIL;

    wchar_t modulePathBuffer[32768]{};
    const DWORD moduleLength = GetModuleFileNameW(moduleHandle(), modulePathBuffer,
        static_cast<DWORD>(std::size(modulePathBuffer)));
    if (moduleLength == 0 || moduleLength >= std::size(modulePathBuffer)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_INSUFFICIENT_BUFFER : error);
    }

    const std::wstring baseKey = L"Software\\Classes\\CLSID\\" + clsidString;
    HRESULT hr = setRegistryStringValue(HKEY_CURRENT_USER, baseKey, L"", L"Myanglish IME");
    if (FAILED(hr)) return hr;
    hr = setRegistryStringValue(HKEY_CURRENT_USER, baseKey + L"\\InprocServer32", L"", modulePathBuffer);
    if (FAILED(hr)) return hr;
    return setRegistryStringValue(HKEY_CURRENT_USER, baseKey + L"\\InprocServer32",
                                  L"ThreadingModel", L"Apartment");
}

HRESULT unregisterComServer() {
    const std::wstring clsidString = guidToString(CLSID_MyanglishIME);
    if (clsidString.empty()) return E_FAIL;
    const std::wstring baseKey = L"Software\\Classes\\CLSID\\" + clsidString;
    const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, baseKey.c_str());
    if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        return S_OK;
    return HRESULT_FROM_WIN32(result);
}

HRESULT registerTsfCategories() {
    ITfCategoryMgr* categoryManager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, reinterpret_cast<void**>(&categoryManager));
    if (FAILED(hr)) return hr;

    hr = categoryManager->RegisterCategory(CLSID_MyanglishIME,
        GUID_TFCAT_TIP_KEYBOARD, CLSID_MyanglishIME);
    if (SUCCEEDED(hr))
        hr = categoryManager->RegisterCategory(CLSID_MyanglishIME,
            GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_MyanglishIME);
    if (SUCCEEDED(hr))
        hr = categoryManager->RegisterCategory(CLSID_MyanglishIME,
            GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_MyanglishIME);

    categoryManager->Release();
    return hr;
}

HRESULT unregisterTsfCategories() {
    ITfCategoryMgr* categoryManager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, reinterpret_cast<void**>(&categoryManager));
    if (FAILED(hr)) return hr;

    (void)categoryManager->UnregisterCategory(CLSID_MyanglishIME,
        GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_MyanglishIME);
    (void)categoryManager->UnregisterCategory(CLSID_MyanglishIME,
        GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_MyanglishIME);
    hr = categoryManager->UnregisterCategory(CLSID_MyanglishIME,
        GUID_TFCAT_TIP_KEYBOARD, CLSID_MyanglishIME);
    categoryManager->Release();
    return hr == E_FAIL ? S_OK : hr;
}

HRESULT registerTsfProfile() {
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
        reinterpret_cast<void**>(&profiles));
    debugLogHr("profiles CoCreateInstance", hr);
    if (FAILED(hr)) return hr;

    hr = profiles->Register(CLSID_MyanglishIME);
    debugLogHr("profiles Register", hr);
    if (FAILED(hr)) { profiles->Release(); return hr; }

    wchar_t profileIconPath[32768]{};
    const DWORD profileIconLength = GetModuleFileNameW(moduleHandle(), profileIconPath,
        static_cast<DWORD>(std::size(profileIconPath)));
    if (profileIconLength == 0 || profileIconLength >= std::size(profileIconPath)) {
        const DWORD error = GetLastError();
        profiles->Release();
        return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_INSUFFICIENT_BUFFER : error);
    }

    hr = profiles->AddLanguageProfile(CLSID_MyanglishIME, kMyanglishLangId,
        GUID_MyanglishIMEProfile, kProfileDescription,
        static_cast<ULONG>(std::size(kProfileDescription) - 1),
        profileIconPath, profileIconLength, 0);
    debugLogHr("profiles AddLanguageProfile", hr);
    if (FAILED(hr)) { profiles->Release(); return hr; }

    const HKL usLayout = LoadKeyboardLayoutW(kUsKeyboardLayoutName, KLF_SUBSTITUTE_OK);
    if (usLayout == nullptr) {
        const DWORD error = GetLastError();
        debugLogHr("LoadKeyboardLayout US",
            HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_INVALID_HANDLE : error));
    } else {
        debugLogHr("profiles SubstituteKeyboardLayout",
            profiles->SubstituteKeyboardLayout(CLSID_MyanglishIME, kMyanglishLangId,
                GUID_MyanglishIMEProfile, usLayout));
    }

    hr = profiles->EnableLanguageProfile(CLSID_MyanglishIME, kMyanglishLangId,
        GUID_MyanglishIMEProfile, TRUE);
    debugLogHr("profiles EnableLanguageProfile", hr);
    profiles->Release();
    return hr;
}

HRESULT installLayoutOrTip(DWORD flags) {
    HMODULE inputDll = LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (inputDll == nullptr) {
        const DWORD error = GetLastError();
        const HRESULT hr = HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_MOD_NOT_FOUND : error);
        debugLogHr("LoadLibraryEx input.dll", hr);
        return hr;
    }

    using InstallLayoutOrTipFn = BOOL (WINAPI*)(LPCWSTR, DWORD);
    const auto install = reinterpret_cast<InstallLayoutOrTipFn>(
        GetProcAddress(inputDll, "InstallLayoutOrTip"));
    if (install == nullptr) {
        const DWORD error = GetLastError();
        FreeLibrary(inputDll);
        const HRESULT hr = HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_PROC_NOT_FOUND : error);
        debugLogHr("GetProcAddress InstallLayoutOrTip", hr);
        return hr;
    }

    SetLastError(ERROR_SUCCESS);
    const BOOL ok = install(kMyanglishTip, flags);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    FreeLibrary(inputDll);

    const HRESULT hr = ok ? S_OK :
        HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
    debugLogHr(flags == 0 ? "InstallLayoutOrTip install" : "InstallLayoutOrTip uninstall", hr);
    return hr;
}

HRESULT unregisterTsfProfile() {
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
        reinterpret_cast<void**>(&profiles));
    if (FAILED(hr)) return hr;

    const HRESULT disableResult = profiles->EnableLanguageProfile(CLSID_MyanglishIME,
        kMyanglishLangId, GUID_MyanglishIMEProfile, FALSE);
    const HRESULT removeResult = profiles->RemoveLanguageProfile(CLSID_MyanglishIME,
        kMyanglishLangId, GUID_MyanglishIMEProfile);
    const HRESULT unregisterResult = profiles->Unregister(CLSID_MyanglishIME);
    profiles->Release();

    if (FAILED(disableResult) && disableResult != E_FAIL) return disableResult;
    if (FAILED(removeResult) && removeResult != E_FAIL) return removeResult;
    if (FAILED(unregisterResult) && unregisterResult != E_FAIL) return unregisterResult;
    return S_OK;
}

HRESULT initializeComForRegistration(bool& shouldUninitialize) {
    shouldUninitialize = false;
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) { shouldUninitialize = true; return S_OK; }
    if (hr == RPC_E_CHANGED_MODE) return S_OK;
    return hr;
}

} // namespace

HRESULT registerServer() {
    bool shouldUninitialize = false;
    HRESULT hr = initializeComForRegistration(shouldUninitialize);
    if (FAILED(hr)) return hr;

    debugLog("DllRegisterServer: start");
    hr = registerComServer();
    debugLogHr("registerComServer", hr);
    if (SUCCEEDED(hr)) {
        hr = registerTsfProfile();
        debugLogHr("registerTsfProfile", hr);
    }
    if (SUCCEEDED(hr)) {
        hr = registerTsfCategories();
        debugLogHr("registerTsfCategories", hr);
    }
    if (SUCCEEDED(hr)) {
        hr = installLayoutOrTip(0);
        debugLogHr("installLayoutOrTip", hr);
    }

    if (FAILED(hr)) {
        (void)installLayoutOrTip(kIlotUninstall);
        (void)unregisterTsfCategories();
        (void)unregisterTsfProfile();
        (void)unregisterComServer();
    }

    if (shouldUninitialize) CoUninitialize();
    return hr;
}

HRESULT unregisterServer() {
    bool shouldUninitialize = false;
    HRESULT hr = initializeComForRegistration(shouldUninitialize);
    if (FAILED(hr)) return hr;

    debugLog("DllUnregisterServer: start");
    const HRESULT layoutResult = installLayoutOrTip(kIlotUninstall);
    const HRESULT categoryResult = unregisterTsfCategories();
    const HRESULT profileResult = unregisterTsfProfile();
    const HRESULT comResult = unregisterComServer();

    debugLogHr("uninstallLayoutOrTip", layoutResult);
    debugLogHr("unregisterTsfCategories", categoryResult);
    debugLogHr("unregisterTsfProfile", profileResult);
    debugLogHr("unregisterComServer", comResult);

    if (shouldUninitialize) CoUninitialize();
    if (FAILED(categoryResult)) return categoryResult;
    if (FAILED(profileResult)) return profileResult;
    return comResult;
}

} // namespace myanglish::ime
