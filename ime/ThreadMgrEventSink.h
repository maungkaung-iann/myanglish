#pragma once

#include <atomic>

#include <Windows.h>
#include <msctf.h>

namespace myanglish::ime {

class TextService;

class ThreadMgrEventSink final : public ITfThreadMgrEventSink {
public:
    explicit ThreadMgrEventSink(TextService& service);
    ~ThreadMgrEventSink();

    ThreadMgrEventSink(const ThreadMgrEventSink&) = delete;
    ThreadMgrEventSink& operator=(const ThreadMgrEventSink&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr* documentMgr) override;
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr* documentMgr) override;
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* previousFocus) override;
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext* context) override;
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext* context) override;

private:
    TextService& service_;
    std::atomic<ULONG> refCount_{1};
};

} // namespace myanglish::ime
