#include "ClassFactory.h"

#include "Globals.h"
#include "Guids.h"
#include "TextService.h"

namespace myanglish::ime {

ClassFactory::ClassFactory() {
    addObject();
}

ClassFactory::~ClassFactory() {
    releaseObject();
}

HRESULT STDMETHODCALLTYPE ClassFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) {
        return E_POINTER;
    }

    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppvObject = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ClassFactory::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE ClassFactory::Release() {
    const ULONG newCount = --refCount_;
    if (newCount == 0) {
        delete this;
    }
    return newCount;
}

HRESULT STDMETHODCALLTYPE ClassFactory::CreateInstance(IUnknown* outer, REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) {
        return E_POINTER;
    }

    *ppvObject = nullptr;
    if (outer != nullptr) {
        return CLASS_E_NOAGGREGATION;
    }

    TextService* service = new TextService();
    HRESULT hr = service->QueryInterface(riid, ppvObject);
    service->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE ClassFactory::LockServer(BOOL lock) {
    if (lock) {
        addServerLock();
    } else {
        releaseServerLock();
    }

    return S_OK;
}

} // namespace myanglish::ime