#pragma once

#include <Windows.h>

namespace myanglish::settings {

struct PackagedTsfBootstrapResult {
    HRESULT hr = S_OK;
    const wchar_t* step = L"ready";
};

PackagedTsfBootstrapResult ensurePackagedTsfRegistration();

} // namespace myanglish::settings
