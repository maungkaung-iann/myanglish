#include "KeyEventSink.h"

#include "TextService.h"

namespace myanglish::ime {

KeyEventSink::KeyEventSink(TextService& service)
    : service_(service) {
    addObject();
}

KeyEventSink::~KeyEventSink() {
    releaseObject();
}

HRESULT STDMETHODCALLTYPE KeyEventSink::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) {
        return E_POINTER;
    }

    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfKeyEventSink) {
        *ppvObject = static_cast<ITfKeyEventSink*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE KeyEventSink::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE KeyEventSink::Release() {
    const ULONG newCount = --refCount_;
    if (newCount == 0) {
        delete this;
    }
    return newCount;
}

HRESULT STDMETHODCALLTYPE KeyEventSink::OnSetFocus(BOOL fForeground) {
    return service_.onSetFocus(fForeground);
}

HRESULT STDMETHODCALLTYPE KeyEventSink::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) {
        return E_POINTER;
    }

    *pfEaten = service_.shouldHandleKeyDown(pic, wParam) ? TRUE : FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE KeyEventSink::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) {
        return E_POINTER;
    }

    *pfEaten = FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE KeyEventSink::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) {
        return E_POINTER;
    }

    const HRESULT hr = service_.processKeyDown(pic, wParam);
    if (hr == S_OK) {
        *pfEaten = TRUE;
        return S_OK;
    }

    if (hr == S_FALSE) {
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = FALSE;
    return hr;
}

HRESULT STDMETHODCALLTYPE KeyEventSink::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) {
        return E_POINTER;
    }

    *pfEaten = FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE KeyEventSink::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) {
    if (pfEaten == nullptr) {
        return E_POINTER;
    }

    *pfEaten = FALSE;
    return S_OK;
}

} // namespace myanglish::ime