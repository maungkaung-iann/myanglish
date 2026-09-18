#include "KeyEventSink.h"

#include "TextService.h"

namespace myanglish::ime {

KeyEventSink::KeyEventSink(TextService& service) : service_(service) { addObject(); }
KeyEventSink::~KeyEventSink() { releaseObject(); }

HRESULT STDMETHODCALLTYPE KeyEventSink::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) return E_POINTER;
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfKeyEventSink) {
        *ppvObject = static_cast<ITfKeyEventSink*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE KeyEventSink::AddRef() { return ++refCount_; }
ULONG STDMETHODCALLTYPE KeyEventSink::Release() {
    const ULONG newCount = --refCount_;
    if (newCount == 0) delete this;
    return newCount;
}
HRESULT STDMETHODCALLTYPE KeyEventSink::OnSetFocus(BOOL fForeground) { return service_.onSetFocus(fForeground); }
HRESULT STDMETHODCALLTYPE KeyEventSink::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_POINTER;

    // Latch the physical Shift+CapsLock chord while Shift is definitely down.
    // The CapsLock key-up can arrive after Shift has already been released.
    if (wParam == VK_CAPITAL && (GetKeyState(VK_SHIFT) < 0)) {
        suppressShiftCapsKeyUp_ = true;
    }

    *pfEaten = service_.shouldHandleKeyDown(pic, wParam) ? TRUE : FALSE;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE KeyEventSink::OnTestKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_POINTER;
    *pfEaten = (wParam == VK_CAPITAL && suppressShiftCapsKeyUp_) ? TRUE : FALSE;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE KeyEventSink::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_POINTER;

    // Re-check routing at the real key-down stage. On some JIS/TSF hosts a key
    // that OnTestKeyDown did not claim can still arrive here. Calling
    // processKeyDown unconditionally would then make Myanglish consume an ASCII
    // letter even while plain CapsLock capital bypass is active.
    if (!service_.shouldHandleKeyDown(pic, wParam)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    const HRESULT hr = service_.processKeyDown(pic, wParam);
    if (hr == S_OK) { *pfEaten = TRUE; return S_OK; }
    if (hr == S_FALSE) { *pfEaten = FALSE; return S_OK; }
    *pfEaten = FALSE;
    return hr;
}
HRESULT STDMETHODCALLTYPE KeyEventSink::OnKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_POINTER;

    if (wParam == VK_CAPITAL && suppressShiftCapsKeyUp_) {
        suppressShiftCapsKeyUp_ = false;
        *pfEaten = TRUE;
        return S_OK;
    }

    *pfEaten = FALSE;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE KeyEventSink::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_POINTER;
    *pfEaten = FALSE;
    return S_OK;
}

} // namespace myanglish::ime
