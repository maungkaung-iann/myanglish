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

// Burmese (Myanmar) language ID: 0x0455, my-MM.
constexpr LANGID kMyanglishLangId = 0x0455;
constexpr const wchar_t kProfileDescription[] = L"Myanglish IME";
constexpr const wchar_t kUsKeyboardLayoutName[] = L"00000409";

std::wstring guidToString(REFGUID guid) {
    std::array<wchar_t, 64> buffer{};
    const int length = StringFromGUID2(guid, buffer.data(), static_cast<int>(buffer.size()));
    if (length <= 0) {
        return {};
    }
    return std::wstring(buffer.data());
}

HRESULT setRegistryStringValue(
    HKEY root,
    const std::wstring& subKey,
    const std::wstring& valueName,
    const std::wstring& value
) {
    HKEY key = nullptr;
    const LONG createResult = RegCreateKeyExW(
        root,
        subKey.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &key,
        nullptr
    );

    if (createResult != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(createResult);
    }

    const LONG setResult = RegSetValueExW(
        key,
        valueName.empty() ? nullptr : valueName.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(key);
    return setResult == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(setResult);
}

HRESULT registerComServer() {
    const std::wstring clsidString = guidToString(CLSID_MyanglishIME);
    if (clsidString.empty()) {
        return E_FAIL;
    }

    wchar_t modulePathBuffer[32768] = {};
    const DWORD moduleLength = GetModuleFileNameW(
        moduleHandle(),
        modulePathBuffer,
        static_cast<DWORD>(std::size(modulePathBuffer))
    );

    if (moduleLength == 0 || moduleLength >= std::size(modulePathBuffer)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_INSUFFICIENT_BUFFER : error);
    }

    const std::wstring baseKey = L"Software\\Classes\\CLSID\\" + clsidString;

    HRESULT hr = setRegistryStringValue(HKEY_CURRENT_USER, baseKey, L"", L"Myanglish IME");
    if (FAILED(hr)) {
        return hr;
    }

    hr = setRegistryStringValue(
        HKEY_CURRENT_USER,
        baseKey + L"\\InprocServer32",
        L"",
        modulePathBuffer
    );
    if (FAILED(hr)) {
        return hr;
    }

    return setRegistryStringValue(
        HKEY_CURRENT_USER,
        baseKey + L"\\InprocServer32",
        L"ThreadingModel",
        L"Apartment"
    );
}

HRESULT unregisterComServer() {
    const std::wstring clsidString = guidToString(CLSID_MyanglishIME);
    if (clsidString.empty()) {
        return E_FAIL;
    }

    const std::wstring baseKey = L"Software\\Classes\\CLSID\\" + clsidString;
    const LONG deleteResult = RegDeleteTreeW(HKEY_CURRENT_USER, baseKey.c_str());

    if (deleteResult == ERROR_SUCCESS ||
        deleteResult == ERROR_FILE_NOT_FOUND ||
        deleteResult == ERROR_PATH_NOT_FOUND) {
        return S_OK;
    }

    return HRESULT_FROM_WIN32(deleteResult);
}

HRESULT registerTsfCategories() {
    ITfCategoryMgr* categoryManager = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_CategoryMgr,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr,
        reinterpret_cast<void**>(&categoryManager)
    );

    if (FAILED(hr)) {
        return hr;
    }

    hr = categoryManager->RegisterCategory(
        CLSID_MyanglishIME,
        GUID_TFCAT_TIP_KEYBOARD,
        CLSID_MyanglishIME
    );

    categoryManager->Release();
    return hr;
}

HRESULT unregisterTsfCategories() {
    ITfCategoryMgr* categoryManager = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_CategoryMgr,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr,
        reinterpret_cast<void**>(&categoryManager)
    );

    if (FAILED(hr)) {
        return hr;
    }

    hr = categoryManager->UnregisterCategory(
        CLSID_MyanglishIME,
        GUID_TFCAT_TIP_KEYBOARD,
        CLSID_MyanglishIME
    );

    categoryManager->Release();

    // Make unregistration repeatable during development.
    return hr == E_FAIL ? S_OK : hr;
}

HRESULT registerTsfProfile() {
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles,
        reinterpret_cast<void**>(&profiles)
    );

    if (FAILED(hr)) {
        return hr;
    }

    hr = profiles->Register(CLSID_MyanglishIME);
    if (FAILED(hr)) {
        profiles->Release();
        return hr;
    }

    hr = profiles->AddLanguageProfile(
        CLSID_MyanglishIME,
        kMyanglishLangId,
        GUID_MyanglishIMEProfile,
        kProfileDescription,
        static_cast<ULONG>(std::size(kProfileDescription) - 1),
        nullptr,
        0,
        0
    );

    if (SUCCEEDED(hr)) {
        // A Burmese profile otherwise falls back to the installed Burmese
        // hardware layout (for example Myanmar Visual). Myanglish needs Latin
        // A-Z virtual keys, so use US QWERTY as this TIP's substitute layout.
        const HKL usLayout = LoadKeyboardLayoutW(
            kUsKeyboardLayoutName,
            KLF_SUBSTITUTE_OK
        );

        if (usLayout == nullptr) {
            const DWORD error = GetLastError();
            hr = HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_INVALID_HANDLE : error);
        } else {
            hr = profiles->SubstituteKeyboardLayout(
                CLSID_MyanglishIME,
                kMyanglishLangId,
                GUID_MyanglishIMEProfile,
                usLayout
            );
        }
    }

    if (SUCCEEDED(hr)) {
        hr = profiles->EnableLanguageProfile(
            CLSID_MyanglishIME,
            kMyanglishLangId,
            GUID_MyanglishIMEProfile,
            TRUE
        );
    }

    profiles->Release();
    return hr;
}

HRESULT unregisterTsfProfile() {
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles,
        reinterpret_cast<void**>(&profiles)
    );

    if (FAILED(hr)) {
        return hr;
    }

    // Disable first. Missing profiles can return E_FAIL during repeated dev
    // unregisters, so those cases are intentionally treated as already clean.
    const HRESULT disableResult = profiles->EnableLanguageProfile(
        CLSID_MyanglishIME,
        kMyanglishLangId,
        GUID_MyanglishIMEProfile,
        FALSE
    );

    const HRESULT removeResult = profiles->RemoveLanguageProfile(
        CLSID_MyanglishIME,
        kMyanglishLangId,
        GUID_MyanglishIMEProfile
    );

    const HRESULT unregisterResult = profiles->Unregister(CLSID_MyanglishIME);
    profiles->Release();

    if (FAILED(disableResult) && disableResult != E_FAIL) {
        return disableResult;
    }
    if (FAILED(removeResult) && removeResult != E_FAIL) {
        return removeResult;
    }
    if (FAILED(unregisterResult) && unregisterResult != E_FAIL) {
        return unregisterResult;
    }

    return S_OK;
}

HRESULT initializeComForRegistration(bool& shouldUninitialize) {
    shouldUninitialize = false;
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (SUCCEEDED(hr)) {
        shouldUninitialize = true;
        return S_OK;
    }

    if (hr == RPC_E_CHANGED_MODE) {
        return S_OK;
    }

    return hr;
}

} // namespace

HRESULT registerServer() {
    bool shouldUninitialize = false;
    HRESULT hr = initializeComForRegistration(shouldUninitialize);
    if (FAILED(hr)) {
        return hr;
    }

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

    if (FAILED(hr)) {
        unregisterTsfCategories();
        unregisterTsfProfile();
        unregisterComServer();
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }

    return hr;
}

HRESULT unregisterServer() {
    bool shouldUninitialize = false;
    HRESULT hr = initializeComForRegistration(shouldUninitialize);
    if (FAILED(hr)) {
        return hr;
    }

    debugLog("DllUnregisterServer: start");

    const HRESULT categoryResult = unregisterTsfCategories();
    const HRESULT profileResult = unregisterTsfProfile();
    const HRESULT comResult = unregisterComServer();

    debugLogHr("unregisterTsfCategories", categoryResult);
    debugLogHr("unregisterTsfProfile", profileResult);
    debugLogHr("unregisterComServer", comResult);

    if (shouldUninitialize) {
        CoUninitialize();
    }

    if (FAILED(categoryResult)) {
        return categoryResult;
    }
    if (FAILED(profileResult)) {
        return profileResult;
    }
    return comResult;
}

} // namespace myanglish::ime
