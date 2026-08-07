#pragma once

#include "CompositionManager.h"
#include "Globals.h"

#include <atomic>

#include <Windows.h>
#include <msctf.h>

namespace myanglish::ime {

class KeyEventSink;

class TextService final : public ITfTextInputProcessorEx {
public:
    TextService();
    ~TextService();

    TextService(const TextService&) = delete;
    TextService& operator=(const TextService&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* threadMgr, TfClientId clientId) override;
    HRESULT STDMETHODCALLTYPE Deactivate() override;
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) override;

    bool shouldHandleKeyDown(ITfContext* context, WPARAM keyCode) const noexcept;
    HRESULT processKeyDown(ITfContext* context, WPARAM keyCode);
    HRESULT onSetFocus(BOOL foreground);

private:
    HRESULT activateInternal(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags);
    HRESULT deactivateInternal();

    bool isShiftPressed() const noexcept;
    static bool isAsciiLetter(WPARAM keyCode) noexcept;
    static wchar_t toLowerAsciiKey(WPARAM keyCode) noexcept;

    std::atomic<ULONG> refCount_{1};
    ITfThreadMgr* threadMgr_ = nullptr;
    ITfKeystrokeMgr* keystrokeMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    bool active_ = false;
    bool enabled_ = true;
    CompositionManager compositionManager_;
    KeyEventSink* keyEventSink_ = nullptr;
};

} // namespace myanglish::ime