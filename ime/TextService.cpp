#include "TextService.h"

#include "Guids.h"
#include "KeyEventSink.h"

#include <new>

namespace myanglish::ime {

namespace {

bool isSpaceToggle(WPARAM keyCode) {
    return keyCode == VK_SPACE && (GetKeyState(VK_SHIFT) < 0);
}

} // namespace

TextService::TextService()
    : compositionManager_(resolveDataRoot()) {
    addObject();
    debugLog(compositionManager_.isReady()
        ? "TextService created; converter ready"
        : "TextService created; converter NOT ready");
}

TextService::~TextService() {
    if (keyEventSink_ != nullptr) {
        keyEventSink_->Release();
        keyEventSink_ = nullptr;
    }

    if (keystrokeMgr_ != nullptr) {
        keystrokeMgr_->Release();
        keystrokeMgr_ = nullptr;
    }

    if (threadMgr_ != nullptr) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }

    releaseObject();
}

HRESULT STDMETHODCALLTYPE TextService::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) {
        return E_POINTER;
    }

    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx) {
        *ppvObject = static_cast<ITfTextInputProcessorEx*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE TextService::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE TextService::Release() {
    const ULONG newCount = --refCount_;
    if (newCount == 0) {
        delete this;
    }
    return newCount;
}

HRESULT STDMETHODCALLTYPE TextService::Activate(ITfThreadMgr* threadMgr, TfClientId clientId) {
    return activateInternal(threadMgr, clientId, 0);
}

HRESULT STDMETHODCALLTYPE TextService::Deactivate() {
    const HRESULT hr = deactivateInternal();
    active_ = false;
    return hr;
}

HRESULT STDMETHODCALLTYPE TextService::ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) {
    return activateInternal(threadMgr, clientId, flags);
}

HRESULT TextService::activateInternal(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) {
    if (active_) {
        return S_OK;
    }

    if (threadMgr == nullptr) {
        return E_POINTER;
    }

    debugLog("TextService activation started");

    threadMgr_ = threadMgr;
    threadMgr_->AddRef();
    clientId_ = clientId;
    compositionManager_.setClientId(clientId_);

    HRESULT hr = threadMgr_->QueryInterface(
        IID_ITfKeystrokeMgr,
        reinterpret_cast<void**>(&keystrokeMgr_)
    );

    if (FAILED(hr)) {
        debugLogHr("QueryInterface(ITfKeystrokeMgr)", hr);
        compositionManager_.setClientId(TF_CLIENTID_NULL);
        clientId_ = TF_CLIENTID_NULL;
        threadMgr_->Release();
        threadMgr_ = nullptr;
        return hr;
    }

    keyEventSink_ = new (std::nothrow) KeyEventSink(*this);
    if (keyEventSink_ == nullptr) {
        keystrokeMgr_->Release();
        keystrokeMgr_ = nullptr;
        compositionManager_.setClientId(TF_CLIENTID_NULL);
        clientId_ = TF_CLIENTID_NULL;
        threadMgr_->Release();
        threadMgr_ = nullptr;
        return E_OUTOFMEMORY;
    }

    hr = keystrokeMgr_->AdviseKeyEventSink(clientId_, keyEventSink_, TRUE);
    if (FAILED(hr)) {
        debugLogHr("AdviseKeyEventSink", hr);
        keyEventSink_->Release();
        keyEventSink_ = nullptr;
        keystrokeMgr_->Release();
        keystrokeMgr_ = nullptr;
        compositionManager_.setClientId(TF_CLIENTID_NULL);
        clientId_ = TF_CLIENTID_NULL;
        threadMgr_->Release();
        threadMgr_ = nullptr;
        return hr;
    }

    enabled_ = true;
    active_ = true;
    debugLog(std::string("TextService activated; flags=") + std::to_string(flags));
    return S_OK;
}

HRESULT TextService::deactivateInternal() {
    debugLog("TextService deactivating");

    if (threadMgr_ != nullptr) {
        ITfDocumentMgr* focusDoc = nullptr;
        if (SUCCEEDED(threadMgr_->GetFocus(&focusDoc)) && focusDoc != nullptr) {
            ITfContext* context = nullptr;
            if (SUCCEEDED(focusDoc->GetTop(&context)) && context != nullptr) {
                const HRESULT cancelResult = compositionManager_.cancel(context);
                if (FAILED(cancelResult)) {
                    debugLogHr("Cancel composition during deactivation", cancelResult);
                    compositionManager_.clearWithoutContext();
                }
                context->Release();
            } else {
                compositionManager_.clearWithoutContext();
            }

            focusDoc->Release();
        } else {
            compositionManager_.clearWithoutContext();
        }
    } else {
        compositionManager_.clearWithoutContext();
    }

    if (keystrokeMgr_ != nullptr && clientId_ != TF_CLIENTID_NULL) {
        const HRESULT unadviseResult = keystrokeMgr_->UnadviseKeyEventSink(clientId_);
        if (FAILED(unadviseResult)) {
            debugLogHr("UnadviseKeyEventSink", unadviseResult);
        }
    }

    if (keyEventSink_ != nullptr) {
        keyEventSink_->Release();
        keyEventSink_ = nullptr;
    }

    if (keystrokeMgr_ != nullptr) {
        keystrokeMgr_->Release();
        keystrokeMgr_ = nullptr;
    }

    if (threadMgr_ != nullptr) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }

    clientId_ = TF_CLIENTID_NULL;
    compositionManager_.setClientId(TF_CLIENTID_NULL);
    active_ = false;
    return S_OK;
}

bool TextService::isShiftPressed() const noexcept {
    return (GetKeyState(VK_SHIFT) < 0);
}

bool TextService::isAsciiLetter(WPARAM keyCode) noexcept {
    // Letter virtual-key codes are VK_A..VK_Z regardless of Shift state.
    return keyCode >= 'A' && keyCode <= 'Z';
}

wchar_t TextService::toLowerAsciiKey(WPARAM keyCode) noexcept {
    const wchar_t wide = static_cast<wchar_t>(keyCode);
    return (wide >= L'A' && wide <= L'Z')
        ? static_cast<wchar_t>(wide - L'A' + L'a')
        : wide;
}

bool TextService::shouldHandleKeyDown(ITfContext*, WPARAM keyCode) const noexcept {
    if (!enabled_) {
        return isSpaceToggle(keyCode);
    }

    if (isSpaceToggle(keyCode) || isAsciiLetter(keyCode)) {
        return true;
    }

    if (keyCode == VK_BACK || keyCode == VK_SPACE || keyCode == VK_RETURN || keyCode == VK_ESCAPE) {
        return compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition();
    }

    return false;
}

HRESULT TextService::processKeyDown(ITfContext* context, WPARAM keyCode) {
    if (context == nullptr) {
        return E_POINTER;
    }

    if (!enabled_) {
        if (isSpaceToggle(keyCode)) {
            enabled_ = true;
            debugLog("Switched to Myanglish mode");
            return S_OK;
        }
        return S_FALSE;
    }

    if (isSpaceToggle(keyCode)) {
        if (compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition()) {
            const HRESULT commitHr = compositionManager_.commitOriginal(context);
            if (FAILED(commitHr)) {
                debugLogHr("Commit original before mode switch", commitHr);
                return commitHr;
            }
        }

        enabled_ = false;
        debugLog("Switched to English mode");
        return S_OK;
    }

    if (isAsciiLetter(keyCode)) {
        const HRESULT hr = compositionManager_.insertCharacter(context, toLowerAsciiKey(keyCode));
        if (SUCCEEDED(hr)) {
            debugLog("Letter handled by composition");
        } else {
            debugLogHr("insertCharacter", hr);
        }
        return hr;
    }

    if (keyCode == VK_BACK) {
        return compositionManager_.deleteBackspace(context);
    }

    if (keyCode == VK_SPACE) {
        const HRESULT hr = compositionManager_.commitBestCandidate(context);
        if (FAILED(hr)) {
            debugLogHr("commitBestCandidate", hr);
        }
        return hr;
    }

    if (keyCode == VK_RETURN) {
        return compositionManager_.commitOriginal(context);
    }

    if (keyCode == VK_ESCAPE) {
        return compositionManager_.cancel(context);
    }

    return S_FALSE;
}

HRESULT TextService::onSetFocus(BOOL foreground) {
    debugLog(foreground ? "KeyEventSink focus gained" : "KeyEventSink focus lost");
    return S_OK;
}

} // namespace myanglish::ime
