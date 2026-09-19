#include "ThreadMgrEventSink.h"

#include "Globals.h"
#include "TextService.h"

namespace myanglish::ime {

ThreadMgrEventSink::ThreadMgrEventSink(TextService& service) : service_(service) {
    addObject();
}

ThreadMgrEventSink::~ThreadMgrEventSink() {
    releaseObject();
}

HRESULT STDMETHODCALLTYPE ThreadMgrEventSink::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) {
        return E_POINTER;
    }
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfThreadMgrEventSink) {
        *ppvObject = static_cast<ITfThreadMgrEventSink*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ThreadMgrEventSink::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE ThreadMgrEventSink::Release() {
    const ULONG count = --refCount_;
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE ThreadMgrEventSink::OnInitDocumentMgr(ITfDocumentMgr*) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ThreadMgrEventSink::OnUninitDocumentMgr(ITfDocumentMgr*) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ThreadMgrEventSink::OnSetFocus(
    ITfDocumentMgr* focus,
    ITfDocumentMgr* previousFocus
) {
    if (focus != previousFocus) {
        service_.onDocumentFocusChanged();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ThreadMgrEventSink::OnPushContext(ITfContext*) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ThreadMgrEventSink::OnPopContext(ITfContext*) {
    return S_OK;
}

} // namespace myanglish::ime
