#include "Registration.h"

#include "Globals.h"
#include "Guids.h"

#include <array>
#include <filesystem>
#include <string>

#include <Windows.h>
#include <Objbase.h>
#include <msctf.h>

namespace myanglish::ime {

namespace {

// Burmese (Myanmar) language ID: 0x0455, my-MM.
constexpr LANGID kMyanglishLangId = 0x0455;
constexpr const wchar_t kProfileDescription[] = L"Myanglish IME";

std::wstring guidToString(REFGUID guid) {
    std::array<wchar_t, 64> buffer{};

    const int length = StringFromGUID2(
        guid,
        buffer.data(),
        static_cast<int>(buffer.size())
    );

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

    const wchar_t* registryValueName =
        valueName.empty() ? nullptr : valueName.c_str();

    const LONG setResult = RegSetValueExW(
        key,
        registryValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>(
            (value.size() + 1) * sizeof(wchar_t)
        )
    );

    RegCloseKey(key);

    if (setResult != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(setResult);
    }

    return S_OK;
}

HRESULT registerComServer() {
    const std::wstring clsidString =
        guidToString(CLSID_MyanglishIME);

    if (clsidString.empty()) {
        return E_FAIL;
    }

    wchar_t modulePathBuffer[32768] = {};

    const DWORD moduleLength = GetModuleFileNameW(
        moduleHandle(),
        modulePathBuffer,
        static_cast<DWORD>(std::size(modulePathBuffer))
    );

    if (
        moduleLength == 0 ||
        moduleLength >= std::size(modulePathBuffer)
    ) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(
            error == ERROR_SUCCESS
                ? ERROR_INSUFFICIENT_BUFFER
                : error
        );
    }

    const std::wstring baseKey =
        L"Software\\Classes\\CLSID\\" + clsidString;

    HRESULT hr = setRegistryStringValue(
        HKEY_CURRENT_USER,
        baseKey,
        L"",
        L"Myanglish IME"
    );

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
    const std::wstring clsidString =
        guidToString(CLSID_MyanglishIME);

    if (clsidString.empty()) {
        return E_FAIL;
    }

    const std::wstring baseKey =
        L"Software\\Classes\\CLSID\\" + clsidString;

    const LONG deleteResult = RegDeleteTreeW(
        HKEY_CURRENT_USER,
        baseKey.c_str()
    );

    if (
        deleteResult != ERROR_SUCCESS &&
        deleteResult != ERROR_FILE_NOT_FOUND &&
        deleteResult != ERROR_PATH_NOT_FOUND
    ) {
        return HRESULT_FROM_WIN32(deleteResult);
    }

    return S_OK;
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
    return hr;
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

    // First register the text service itself.
    hr = profiles->Register(CLSID_MyanglishIME);

    if (SUCCEEDED(hr)) {
        // Then add its Burmese/Myanmar language profile.
        hr = profiles->AddLanguageProfile(
            CLSID_MyanglishIME,
            kMyanglishLangId,
            GUID_MyanglishIMEProfile,
            kProfileDescription,
            static_cast<ULONG>(
                std::size(kProfileDescription) - 1
            ),
            nullptr,
            0,
            0
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

    // Remove the language profile before unregistering the service.
    const HRESULT removeResult =
        profiles->RemoveLanguageProfile(
            CLSID_MyanglishIME,
            kMyanglishLangId,
            GUID_MyanglishIMEProfile
        );

    const HRESULT unregisterResult =
        profiles->Unregister(CLSID_MyanglishIME);

    profiles->Release();

    if (
        FAILED(removeResult) &&
        removeResult != E_FAIL
    ) {
        return removeResult;
    }

    return unregisterResult;
}

HRESULT initializeComForRegistration(
    bool& shouldUninitialize
) {
    shouldUninitialize = false;

    const HRESULT hr = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED
    );

    if (SUCCEEDED(hr)) {
        shouldUninitialize = true;
        return S_OK;
    }

    // COM is already initialized using another apartment model.
    if (hr == RPC_E_CHANGED_MODE) {
        return S_OK;
    }

    return hr;
}

} // namespace

HRESULT registerServer() {
    bool shouldUninitialize = false;

    HRESULT hr =
        initializeComForRegistration(shouldUninitialize);

    if (FAILED(hr)) {
        return hr;
    }

    hr = registerComServer();

    if (SUCCEEDED(hr)) {
        hr = registerTsfProfile();
    }

    if (SUCCEEDED(hr)) {
        hr = registerTsfCategories();
    }

    if (FAILED(hr)) {
        // Roll back partial registration.
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

    HRESULT hr =
        initializeComForRegistration(shouldUninitialize);

    if (FAILED(hr)) {
        return hr;
    }

    const HRESULT categoryResult =
        unregisterTsfCategories();

    const HRESULT profileResult =
        unregisterTsfProfile();

    const HRESULT comResult =
        unregisterComServer();

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