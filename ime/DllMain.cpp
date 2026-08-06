#include "ClassFactory.h"
#include "Globals.h"
#include "Guids.h"
#include "Registration.h"

#include <Windows.h>

namespace myanglish::ime {

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        setModuleHandle(instance);
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        setModuleHandle(nullptr);
    }

    return TRUE;
}

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (ppv == nullptr) {
        return E_POINTER;
    }

    *ppv = nullptr;
    if (rclsid != CLSID_MyanglishIME) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    ClassFactory* factory = new ClassFactory();
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow(void) {
    return (objectCount() == 0 && serverLockCount() == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT STDAPICALLTYPE DllRegisterServer(void) {
    return registerServer();
}

extern "C" HRESULT STDAPICALLTYPE DllUnregisterServer(void) {
    return unregisterServer();
}

} // namespace myanglish::ime