#include "TextService.h"

#include "Guids.h"
#include "KeyEventSink.h"
#include "LanguageBarButton.h"
#include "PersonalDictionaryManager.h"

#include <algorithm>
#include <iterator>
#include <new>
#include <oleauto.h>

namespace myanglish::ime {

namespace {

class CandidateDisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    explicit CandidateDisplayAttributeInfo(bool candidateAvailable = true)
        : candidateAvailable_(candidateAvailable) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out=nullptr;
        if (iid==IID_IUnknown || iid==IID_ITfDisplayAttributeInfo) {
            *out=static_cast<ITfDisplayAttributeInfo*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG v=(ULONG)InterlockedDecrement(&refs_); if(!v) delete this; return v;
    }
    HRESULT STDMETHODCALLTYPE GetGUID(GUID* g) override {
        if(!g) return E_POINTER;
        *g = candidateAvailable_
            ? GUID_MyanglishCandidateAvailable
            : GUID_MyanglishNoCandidate;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* d) override {
        if(!d) return E_POINTER;
        *d=SysAllocString(
            candidateAvailable_
                ? L"Myanglish candidate available"
                : L"Myanglish no candidate"
        );
        return *d?S_OK:E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetAttributeInfo(TF_DISPLAYATTRIBUTE* a) override {
        if(!a) return E_POINTER;
        ZeroMemory(a,sizeof(*a));

        a->crText.type=TF_CT_NONE;
        a->crBk.type=TF_CT_NONE;
        a->crLine.type=TF_CT_SYSCOLOR;
        a->crLine.nIndex=COLOR_WINDOWTEXT;

        if(candidateAvailable_) {
            a->lsStyle=TF_LS_SOLID;
            a->fBoldLine=TRUE;
        } else {
            // Explicitly override Windows' default composition underline.
            a->lsStyle=TF_LS_NONE;
            a->fBoldLine=FALSE;
        }

        a->bAttr=TF_ATTR_INPUT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }

private:
    ~CandidateDisplayAttributeInfo()=default;
    LONG refs_=1;
    bool candidateAvailable_=true;
};

class CandidateDisplayAttributeEnum final : public IEnumTfDisplayAttributeInfo {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(iid==IID_IUnknown || iid==IID_IEnumTfDisplayAttributeInfo) {
            *out=static_cast<IEnumTfDisplayAttributeInfo*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG v=(ULONG)InterlockedDecrement(&refs_); if(!v) delete this; return v;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumTfDisplayAttributeInfo** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        auto* e=new(std::nothrow) CandidateDisplayAttributeEnum();
        if(!e) return E_OUTOFMEMORY;
        e->index_=index_;
        *out=e;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, ITfDisplayAttributeInfo** info, ULONG* fetched) override {
        if(!info) return E_POINTER;
        if(fetched) *fetched=0;
        if(count==0 || index_>=2) return S_FALSE;

        ULONG produced=0;
        while(produced<count && index_<2) {
            auto* item=new(std::nothrow) CandidateDisplayAttributeInfo(index_==0);
            if(!item) {
                for(ULONG i=0;i<produced;++i) info[i]->Release();
                return E_OUTOFMEMORY;
            }
            info[produced++]=item;
            ++index_;
        }

        if(fetched) *fetched=produced;
        return produced==count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        index_=0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        const ULONG remaining=2-index_;
        const ULONG skipped=(count<remaining)?count:remaining;
        index_+=skipped;
        return skipped==count ? S_OK : S_FALSE;
    }
private:
    ~CandidateDisplayAttributeEnum()=default;
    LONG refs_=1;
    ULONG index_=0;
};


bool isModeToggle(WPARAM keyCode) {
    // Shift+Space toggles Myanglish <-> English.
    // CapsLock is reserved for normal Windows Capital Lock + LED behavior.
    return keyCode == VK_SPACE
        && (GetKeyState(VK_SHIFT) < 0)
        && (GetKeyState(VK_CONTROL) >= 0)
        && (GetKeyState(VK_MENU) >= 0)
        && (GetKeyState(VK_LWIN) >= 0)
        && (GetKeyState(VK_RWIN) >= 0);
}

bool translatedCharacterForKey(WPARAM keyCode, wchar_t& character) {
    BYTE keyboardState[256]{};
    if (!GetKeyboardState(keyboardState)) {
        return false;
    }

    const HKL layout = GetKeyboardLayout(0);
    const UINT virtualKey = static_cast<UINT>(keyCode);
    const UINT scanCode = MapVirtualKeyExW(virtualKey, MAPVK_VK_TO_VSC, layout);

    wchar_t buffer[8]{};
    // Flag 0x4 asks ToUnicodeEx not to mutate the keyboard's dead-key state
    // on supported Windows versions. This makes the lookup safe for a TSF IME.
    const int count = ToUnicodeEx(
        virtualKey,
        scanCode,
        keyboardState,
        buffer,
        static_cast<int>(std::size(buffer)),
        0x4,
        layout
    );

    if (count <= 0) {
        return false;
    }

    character = buffer[0];
    return true;
}

bool isMyanmarPunctuationCharacter(wchar_t character) {
    return character == L','
        || character == L'.'
        || character == L':'
        || character == L';'
        || character == L'"'
        || character == L'\\';
}

bool isMyanmarPunctuationKey(WPARAM keyCode) {
    wchar_t character = 0;
    return translatedCharacterForKey(keyCode, character)
        && isMyanmarPunctuationCharacter(character);
}

wchar_t punctuationForCharacter(wchar_t character, bool& smartQuoteOpen) {
    if (character == L',') return static_cast<wchar_t>(0x104A); // ၊
    if (character == L'.') return static_cast<wchar_t>(0x104B); // ။
    if (character == L':') return static_cast<wchar_t>(0x1038); // း
    if (character == L';') return static_cast<wchar_t>(0x1037); // ့

    const wchar_t quote = smartQuoteOpen
        ? static_cast<wchar_t>(0x201C)
        : static_cast<wchar_t>(0x201D);
    smartQuoteOpen = !smartQuoteOpen;
    return quote; // " -> “ then ”
}

bool isRawWordCommit(WPARAM keyCode) {
    return keyCode == VK_SPACE
        && (GetKeyState(VK_CONTROL) < 0)
        && (GetKeyState(VK_SHIFT) >= 0)
        && (GetKeyState(VK_MENU) >= 0)
        && (GetKeyState(VK_LWIN) >= 0)
        && (GetKeyState(VK_RWIN) >= 0);
}


bool isControlPhysicallyHeld() {
    return (GetKeyState(VK_CONTROL) < 0)
        || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
        || ((GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0)
        || ((GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0);
}

bool isStackCommitShortcut(WPARAM keyCode) {
    return keyCode == VK_RETURN
        && isControlPhysicallyHeld()
        && (GetKeyState(VK_SHIFT) >= 0)
        && (GetKeyState(VK_MENU) >= 0)
        && (GetKeyState(VK_LWIN) >= 0)
        && (GetKeyState(VK_RWIN) >= 0);
}

bool isAsciiDigitKey(WPARAM keyCode) {
    return keyCode >= '0' && keyCode <= '9';
}

bool isR111ShiftHeld() {
    return GetKeyState(VK_SHIFT) < 0;
}

bool isR111MyanmarDigitKey(WPARAM keyCode) {
    return isAsciiDigitKey(keyCode) && !isR111ShiftHeld();
}

bool isR111ArrowKey(WPARAM keyCode) {
    return keyCode == VK_LEFT || keyCode == VK_RIGHT
        || keyCode == VK_UP || keyCode == VK_DOWN;
}

bool isR111CustomSymbol(wchar_t c) {
    return c == L',' || c == L'.' || c == L':' || c == L';'
        || c == L'"' || c == L'\\' || c == L'|' || c == L'*';
}

bool isR111PassThroughSymbol(WPARAM keyCode) {
    wchar_t c = 0;
    if (!translatedCharacterForKey(keyCode, c) || isR111CustomSymbol(c)) {
        return false;
    }
    return (c >= 0x21 && c <= 0x2F)
        || (c >= 0x3A && c <= 0x40)
        || (c >= 0x5B && c <= 0x60)
        || (c >= 0x7B && c <= 0x7E);
}

wchar_t myanmarDigitForKey(WPARAM keyCode) {
    // Official Unicode Myanmar digits U+1040..U+1049.
    // Glyph size is intentionally left to the host application's current font,
    // so Myanglish does not apply a smaller IME-specific visual style.
    return static_cast<wchar_t>(0x1040 + (keyCode - '0'));
}

bool baseCharacterForKey(WPARAM keyCode, wchar_t& character) {
    BYTE keyboardState[256]{};
    if (!GetKeyboardState(keyboardState)) {
        return false;
    }

    keyboardState[VK_SHIFT] = 0;
    keyboardState[VK_LSHIFT] = 0;
    keyboardState[VK_RSHIFT] = 0;

    const HKL layout = GetKeyboardLayout(0);
    const UINT virtualKey = static_cast<UINT>(keyCode);
    const UINT scanCode = MapVirtualKeyExW(virtualKey, MAPVK_VK_TO_VSC, layout);

    wchar_t buffer[8]{};
    const int count = ToUnicodeEx(
        virtualKey,
        scanCode,
        keyboardState,
        buffer,
        static_cast<int>(std::size(buffer)),
        0x4,
        layout
    );
    if (count <= 0) {
        return false;
    }
    character = buffer[0];
    return true;
}

bool isSemicolonBaseKey(WPARAM keyCode) {
    wchar_t base = 0;
    return baseCharacterForKey(keyCode, base) && base == L';';
}

bool isTypedStarKey(WPARAM keyCode) {
    wchar_t typed = 0;
    return translatedCharacterForKey(keyCode, typed) && typed == L'*';
}

} // namespace

TextService::TextService()
    : compositionManager_(resolveDataRoot()) {
    liveCandidatesEnabled_ = liveCandidatePopupEnabled();
    candidateWindow_.setForegroundLossCallback(
        [](void* context) {
            if (context != nullptr) {
                static_cast<TextService*>(context)->handleCandidateForegroundLoss();
            }
        },
        this
    );

    candidateWindow_.setManageWordsCallback(
        [](void* context) {
            if (context != nullptr) {
                static_cast<TextService*>(context)->handleManageWordsRequested();
            }
        },
        this
    );

    candidateWindow_.setSelectionChangedCallback(
        [](void* context, std::size_t index) {
            if (context != nullptr) {
                static_cast<TextService*>(context)->handleCandidateMouseSelection(index);
            }
        },
        this
    );

    candidateWindow_.setCandidateCommitCallback(
        [](void* context, std::size_t index) {
            if (context != nullptr) {
                static_cast<TextService*>(context)->handleCandidateMouseCommit(index);
            }
        },
        this
    );
    addObject();
    debugLog(compositionManager_.isReady()
        ? "TextService created; converter ready"
        : "TextService created; converter NOT ready");
}

TextService::~TextService() {
    candidateWindow_.hide();
    releaseRememberedContext();

    if (languageBarItemMgr_ != nullptr && languageBarButton_ != nullptr) {
        const HRESULT removeHr = languageBarItemMgr_->RemoveItem(languageBarButton_);
        if (FAILED(removeHr)) {
            debugLogHr("ITfLangBarItemMgr::RemoveItem", removeHr);
        }
    }
    if (languageBarButton_ != nullptr) {
        languageBarButton_->Release();
        languageBarButton_ = nullptr;
    }
    if (languageBarItemMgr_ != nullptr) {
        languageBarItemMgr_->Release();
        languageBarItemMgr_ = nullptr;
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
    if (riid == IID_ITfDisplayAttributeProvider) {
        *ppvObject = static_cast<ITfDisplayAttributeProvider*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE TextService::AddRef() { return ++refCount_; }

ULONG STDMETHODCALLTYPE TextService::Release() {
    const ULONG count = --refCount_;
    if (count == 0) {
        delete this;
    }
    return count;
}


HRESULT STDMETHODCALLTYPE TextService::EnumDisplayAttributeInfo(
    IEnumTfDisplayAttributeInfo** enumInfo
) {
    if(!enumInfo) return E_POINTER;
    *enumInfo=new(std::nothrow) CandidateDisplayAttributeEnum();
    return *enumInfo?S_OK:E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE TextService::GetDisplayAttributeInfo(
    REFGUID guid, ITfDisplayAttributeInfo** info
) {
    if(!info) return E_POINTER; *info=nullptr;
    if(IsEqualGUID(guid,GUID_MyanglishCandidateAvailable)) {
        *info=new(std::nothrow) CandidateDisplayAttributeInfo(true);
        return *info?S_OK:E_OUTOFMEMORY;
    }

    if(IsEqualGUID(guid,GUID_MyanglishNoCandidate)) {
        *info=new(std::nothrow) CandidateDisplayAttributeInfo(false);
        return *info?S_OK:E_OUTOFMEMORY;
    }

    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE TextService::Activate(ITfThreadMgr* threadMgr, TfClientId clientId) {
    return activateInternal(threadMgr, clientId, 0);
}

HRESULT STDMETHODCALLTYPE TextService::ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) {
    return activateInternal(threadMgr, clientId, flags);
}

HRESULT STDMETHODCALLTYPE TextService::Deactivate() {
    const HRESULT hr = deactivateInternal();
    active_ = false;
    return hr;
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
    liveCandidatesEnabled_ = liveCandidatePopupEnabled();

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

    // Alpha 0.10.8: add A / မြန် input-mode button to the Windows
    // language/taskbar input area.
    hr = threadMgr_->QueryInterface(
        IID_ITfLangBarItemMgr,
        reinterpret_cast<void**>(&languageBarItemMgr_)
    );
    if (SUCCEEDED(hr) && languageBarItemMgr_ != nullptr) {
        languageBarButton_ = new (std::nothrow) LanguageBarButton(*this);
        if (languageBarButton_ != nullptr) {
            hr = languageBarItemMgr_->AddItem(languageBarButton_);
            if (FAILED(hr)) {
                debugLogHr("ITfLangBarItemMgr::AddItem", hr);
                languageBarButton_->Release();
                languageBarButton_ = nullptr;
            } else {
                debugLog("A / မြန် language-bar switch added");
            }
        }
    } else {
        debugLogHr("QueryInterface(ITfLangBarItemMgr)", hr);
        if (languageBarItemMgr_ != nullptr) {
            languageBarItemMgr_->Release();
            languageBarItemMgr_ = nullptr;
        }
    }

    enabled_ = true;
    active_ = true;
    debugLog(std::string("TextService activated; flags=") + std::to_string(flags));
    return S_OK;
}

HRESULT TextService::deactivateInternal() {
    debugLog("TextService deactivating");
    candidateWindow_.hide();

    // Never leave a popup or TSF composition attached to the old application.
    // Alpha 0.8.9 finalizes the current raw/converted composition before
    // dropping local state so it cannot leak into another application.
    (void)commitVisibleOnFocusLoss();
    releaseRememberedContext();

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

void TextService::rememberContext(ITfContext* context) noexcept {
    if (context == lastContext_) {
        return;
    }
    releaseRememberedContext();
    lastContext_ = context;
    if (lastContext_ != nullptr) {
        lastContext_->AddRef();
    }
}

void TextService::releaseRememberedContext() noexcept {
    if (lastContext_ != nullptr) {
        lastContext_->Release();
        lastContext_ = nullptr;
    }
}

HRESULT TextService::commitVisibleOnFocusLoss() noexcept {
    if (!compositionManager_.hasBufferedText() && !compositionManager_.hasActiveComposition()) {
        return S_FALSE;
    }
    if (lastContext_ == nullptr) {
        compositionManager_.clearWithoutContext();
        return S_FALSE;
    }

    // Alpha 0.8.8 keeps raw Myanglish visible until Space starts conversion.
    // On focus loss, commit exactly what this word state represents, then clear
    // every candidate/conversion flag so no other app can inherit stale state.
    HRESULT hr = S_FALSE;
    if (conversionActive_ || candidateSelectionActive_) {
        hr = compositionManager_.isRawCandidate(selectedCandidateIndex_)
            ? compositionManager_.commitOriginalAndInsertLiteral(lastContext_, L' ')
            : compositionManager_.commitCandidate(lastContext_, selectedCandidateIndex_);
    } else {
        hr = compositionManager_.commitOriginal(lastContext_);
    }

    if (FAILED(hr) && hr != S_FALSE) {
        debugLogHr("commit visible word on focus loss", hr);
        compositionManager_.clearWithoutContext();
    }
    return hr;
}

void TextService::handleCandidateForegroundLoss() noexcept {
    (void)commitVisibleOnFocusLoss();
    candidateSelectionActive_ = false;
    conversionActive_ = false;
    selectedCandidateIndex_ = 0;
    releaseRememberedContext();
    debugLog("Foreground guard force-cleared popup, conversion, selection, and old-window composition state");
}


void TextService::handleCandidateMouseSelection(
    std::size_t index
) noexcept {
    if (lastContext_ == nullptr
        || !candidateWindow_.isVisible()
        || index >= candidateWindow_.candidateCount()) {
        return;
    }

    selectedCandidateIndex_ = index;
    candidateSelectionActive_ = true;
    conversionActive_ = true;

    const HRESULT hr =
        compositionManager_.previewCandidate(
            lastContext_,
            index
        );

    if (FAILED(hr)) {
        debugLogHr(
            "mouse preview candidate",
            hr
        );
    }
}

void TextService::handleCandidateMouseCommit(
    std::size_t index
) noexcept {
    if (lastContext_ == nullptr
        || !candidateWindow_.isVisible()
        || index >= candidateWindow_.candidateCount()) {
        return;
    }

    selectedCandidateIndex_ = index;
    candidateSelectionActive_ = true;
    conversionActive_ = true;

    const HRESULT hr =
        commitCandidateByNumber(
            lastContext_,
            index
        );

    if (FAILED(hr) && hr != S_FALSE) {
        debugLogHr(
            "mouse commit candidate",
            hr
        );
    }

    candidateWindow_.hide();
    candidateSelectionActive_ = false;
    conversionActive_ = false;
    selectedCandidateIndex_ = 0;
    stackMode_ = false;
    compositionManager_.setStackPrefixEnabled(false);
}

void TextService::handleManageWordsRequested() noexcept {
    // Opening Settings changes windows. Finalize exactly the candidate the user
    // currently sees before leaving the editor, then open the dictionary UI.
    (void)commitVisibleOnFocusLoss();

    candidateWindow_.hide();
    candidateSelectionActive_ = false;
    conversionActive_ = false;
    selectedCandidateIndex_ = 0;
    stackMode_ = false;
    compositionManager_.setStackPrefixEnabled(false);

    showPersonalDictionaryManager();
}

bool TextService::isShortcutModifierPressed() noexcept {
    return (GetKeyState(VK_CONTROL) < 0)
        || (GetKeyState(VK_MENU) < 0)
        || (GetKeyState(VK_LWIN) < 0)
        || (GetKeyState(VK_RWIN) < 0);
}

bool TextService::isAsciiLetter(WPARAM keyCode) noexcept {
    return keyCode >= 'A' && keyCode <= 'Z';
}

bool TextService::isCandidateNumberKey(WPARAM keyCode) noexcept {
    return keyCode >= '1' && keyCode <= '9';
}

bool TextService::isHostEditingShortcut(WPARAM keyCode) noexcept {
    if ((GetKeyState(VK_CONTROL) >= 0) || (GetKeyState(VK_MENU) < 0)
        || (GetKeyState(VK_LWIN) < 0) || (GetKeyState(VK_RWIN) < 0)) {
        return false;
    }
    // These shortcuts act on the host document selection. Before passing them
    // through, active IME composition text must become ordinary document text so
    // Ctrl+A/C/X/V cannot leave an unselected/un-cut preview behind.
    return keyCode == 'A' || keyCode == 'C' || keyCode == 'X' || keyCode == 'V';
}

wchar_t TextService::toLowerAsciiKey(WPARAM keyCode) noexcept {
    const wchar_t wide = static_cast<wchar_t>(keyCode);
    return (wide >= L'A' && wide <= L'Z')
        ? static_cast<wchar_t>(wide - L'A' + L'a')
        : wide;
}

HRESULT TextService::toggleModeFromLanguageBar() noexcept {
    candidateWindow_.hide();

    // If a composition is active, accept what the user currently sees before
    // changing the mode. This keeps taskbar clicks from leaving stale TSF state.
    if (compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition()) {
        (void)commitVisibleOnFocusLoss();
    }

    candidateSelectionActive_ = false;
    conversionActive_ = false;
    selectedCandidateIndex_ = 0;
    stackMode_ = false;
    compositionManager_.setStackPrefixEnabled(false);

    enabled_ = !enabled_;

    if (languageBarButton_ != nullptr) {
        languageBarButton_->notifyModeChanged();
    }

    debugLog(enabled_
        ? "Language-bar switch: Myanglish mode"
        : "Language-bar switch: English mode");
    return S_OK;
}

bool TextService::shouldHandleKeyDown(ITfContext*, WPARAM keyCode) noexcept {
    // The resume window is exactly one physical key. Any key other than the
    // immediate Backspace expires it, including keys that the IME passes through
    // to the host (arrows, punctuation, shortcuts, etc.).
    if (keyCode != VK_BACK && compositionManager_.hasPendingRawAutoSpace()) {
        compositionManager_.cancelPendingRawAutoSpace();
    }
    if (isStackCommitShortcut(keyCode)) {
        const bool handle = enabled_ && compositionManager_.hasBufferedText();
        stackShortcutPending_ = handle;
        return handle;
    }
    if (isRawWordCommit(keyCode)) {
        return enabled_ && compositionManager_.hasBufferedText();
    }
    if (isHostEditingShortcut(keyCode)) {
        return enabled_
            && (compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition());
    }
    if (isShortcutModifierPressed()) {
        return false;
    }
    if (!enabled_) {
        return isModeToggle(keyCode);
    }
    // Alpha 0.9.3: Shift by itself is never eaten. Stack mode is armed only
    // when an ASCII letter is actually typed while Shift is physically held.
    if (keyCode == VK_SHIFT || keyCode == VK_LSHIFT || keyCode == VK_RSHIFT) {
        return false;
    }
    if (
        keyCode == VK_CAPITAL
        && (GetKeyState(VK_SHIFT) >= 0)
    ) {
        // Latch the decision in OnTestKeyDown. OnKeyDown may observe the
        // CapsLock state after Windows/JIS has already changed it.
        plainCapsKeyPending_ = true;
        return true;
    }
    if (isModeToggle(keyCode)) {
        return true;
    }

    // Smart undo-space: after a raw word was committed with the IME-added
    // trailing Space, the immediately following Backspace belongs to the IME
    // even though there is no active composition at this instant.
    if (keyCode == VK_BACK && compositionManager_.hasPendingRawAutoSpace()) {
        return true;
    }

    if (candidateWindow_.isVisible()) {
        if (keyCode == VK_SPACE || keyCode == VK_RETURN || keyCode == VK_ESCAPE
            || keyCode == VK_BACK || isR111ArrowKey(keyCode)
            || keyCode == VK_TAB || isCandidateNumberKey(keyCode) || isAsciiLetter(keyCode)) {
            return true;
        }
    }

    if (isAsciiLetter(keyCode)) {
        // While plain CapsLock mode is active, Roman letters belong to Windows,
        // not to Myanglish conversion. The host produces normal capital text.
        return !plainCapsCapitalMode_;
    }
    if (isR111MyanmarDigitKey(keyCode)) {
        return true;
    }
    if (isSemicolonBaseKey(keyCode)) {
        return true;
    }
    if (isTypedStarKey(keyCode) && compositionManager_.hasBufferedText()) {
        return true;
    }
    if (isMyanmarPunctuationKey(keyCode)) {
        return true;
    }
    if (isR111PassThroughSymbol(keyCode)) {
        return compositionManager_.hasBufferedText()
            || compositionManager_.hasActiveComposition()
            || conversionActive_ || candidateSelectionActive_;
    }
    if (keyCode == VK_BACK || keyCode == VK_SPACE || keyCode == VK_RETURN
        || keyCode == VK_ESCAPE || isR111ArrowKey(keyCode)
        || keyCode == VK_TAB) {
        return compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition();
    }
    return false;
}

bool TextService::openCandidateWindow() {
    const auto candidates = compositionManager_.currentCandidateTexts(9);
    if (candidates.empty()) {
        candidateWindow_.hide();
        return false;
    }
    RECT textRect{};
    const RECT* anchor = compositionManager_.lastTextRect(textRect) ? &textRect : nullptr;
    if (!candidateWindow_.show(candidates, 0, anchor)) {
        return false;
    }
    debugLog("Candidate window opened by explicit selection key");
    return true;
}

void TextService::refreshCandidateWindow() {
    // alpha-0.8.8: raw Myanglish is visible while typing. Candidate popup stays
    // hidden on typing and on the FIRST Space. The SECOND Space opens it at #2.
    candidateWindow_.hide();
}

HRESULT TextService::moveCandidateSelection(int delta) {
    if (!candidateWindow_.isVisible() || candidateWindow_.candidateCount() == 0) {
        return S_FALSE;
    }
    const std::size_t count = candidateWindow_.candidateCount();
    const std::size_t current = candidateWindow_.selectedIndex();
    std::size_t next = current;
    if (delta > 0) {
        next = (current + 1) % count;
    } else if (delta < 0) {
        next = current == 0 ? count - 1 : current - 1;
    }
    candidateWindow_.setSelection(next);
    selectedCandidateIndex_ = next;
    return S_OK;
}

HRESULT TextService::commitSelectedCandidate(ITfContext* context) {
    if (!candidateWindow_.isVisible()) {
        return S_FALSE;
    }
    const std::size_t index = candidateWindow_.selectedIndex();
    (void)index;
    const HRESULT hr = compositionManager_.commitVisiblePreview(context);
    if (SUCCEEDED(hr)) {
        candidateWindow_.hide();
    }
    return hr;
}

HRESULT TextService::commitCandidateByNumber(ITfContext* context, std::size_t index) {
    if (context == nullptr || !candidateWindow_.isVisible()
        || index >= candidateWindow_.candidateCount()) {
        return S_FALSE;
    }

    candidateWindow_.setSelection(index);
    selectedCandidateIndex_ = index;
    const HRESULT previewHr = compositionManager_.previewCandidate(context, index);
    if (FAILED(previewHr)) {
        return previewHr;
    }
    const HRESULT hr = compositionManager_.commitVisiblePreview(context);
    if (SUCCEEDED(hr)) {
        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
    }
    return hr;
}

HRESULT TextService::processKeyDown(ITfContext* context, WPARAM keyCode) {
    if (context == nullptr) {
        return E_POINTER;
    }

    // Alpha 0.10.2: OnTestKeyDown may see Ctrl held, while some hosts report
    // modifier state differently by the time OnKeyDown arrives. Latch the
    // shortcut decision from OnTestKeyDown so Ctrl+Enter cannot degrade into
    // plain Enter.
    const bool latchedStackShortcut =
        keyCode == VK_RETURN && stackShortcutPending_;
    stackShortcutPending_ = false;

    // Some hosts do not reliably send the TSF focus callback when the user
    // changes top-level windows. CandidateWindow has its own foreground guard;
    // if that guard had to hide the popup, finish/drop the stale old-word state
    // before accepting a key in this service again.
    if (candidateWindow_.consumeForegroundLoss()) {
        (void)commitVisibleOnFocusLoss();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        releaseRememberedContext();
        debugLog("Foreground guard cleared stale candidate/conversion/composition state");
    }

    rememberContext(context);

    auto recover = [this](HRESULT hr, const char* name) -> HRESULT {
        if (FAILED(hr)) {
            debugLogHr(name, hr);
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            candidateWindow_.hide();
            compositionManager_.clearWithoutContext();
            return S_OK;
        }
        return hr;
    };

    // Alpha 0.9.0 universal selection ownership rule. A mouse click or
    // host selection change can move the caret away while TSF still keeps our old
    // composition object alive. Detect that before interpreting Backspace/Space
    // as IME commands. If detached, accept the old visible word in place and let
    // the current non-letter key operate normally at the user's new caret.
    const HRESULT detachHr = compositionManager_.hasActiveComposition()
        ? compositionManager_.finalizeIfSelectionMoved(context)
        : S_FALSE;
    if (FAILED(detachHr)) {
        debugLogHr("finalizeIfSelectionMoved", detachHr);
    } else if (detachHr == S_OK) {
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        candidateWindow_.hide();

        if (!isAsciiLetter(keyCode)
            && !isModeToggle(keyCode)
            && !latchedStackShortcut
            && !isStackCommitShortcut(keyCode)) {
            return S_FALSE;
        }
    }

    // Host editing shortcuts must see composition text as ordinary document text.
    // Commit the currently visible form first, then return S_FALSE so Windows/the
    // application receives the original Ctrl+A/C/X/V keystroke unchanged.
    if (isHostEditingShortcut(keyCode)) {
        HRESULT shortcutCommit = S_FALSE;
        if (conversionActive_ || candidateSelectionActive_) {
            shortcutCommit = compositionManager_.commitCandidate(context, selectedCandidateIndex_);
        } else if (compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition()) {
            shortcutCommit = compositionManager_.commitOriginal(context);
        }

        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        candidateWindow_.hide();

        if (FAILED(shortcutCommit) && shortcutCommit != S_FALSE) {
            debugLogHr("commit composition before host shortcut", shortcutCommit);
        }
        return S_FALSE;
    }

    // Alpha 0.10.1: Ctrl+Enter stacks the best current syllable onto the
    // immediately preceding Burmese consonant and commits the result.
    if (latchedStackShortcut || isStackCommitShortcut(keyCode)) {
        if (!enabled_ || !compositionManager_.hasBufferedText()) {
            return S_FALSE;
        }
        const std::size_t stackCandidateIndex = selectedCandidateIndex_;
        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        debugLog(
            latchedStackShortcut
                ? "Ctrl+Enter stack shortcut accepted from OnTestKeyDown latch"
                : "Ctrl+Enter stack shortcut accepted from live modifier state"
        );
        const HRESULT kinziHr =
            compositionManager_.commitKinziShortcut(
                context,
                stackCandidateIndex
            );

        if (kinziHr == S_OK) {
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            return S_OK;
        }

        if (
            FAILED(kinziHr)
            && kinziHr != S_FALSE
        ) {
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            return recover(
                kinziHr,
                "Ctrl+Enter kinzi shortcut"
            );
        }

        const HRESULT stackHr =
            compositionManager_.commitStackShortcut(
                context,
                stackCandidateIndex
            );

        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);

        return recover(
            stackHr,
            "Ctrl+Enter stack shortcut"
        );
    }

    // Ctrl+Space = keep/commit this one word as literal Roman text.
    if (isRawWordCommit(keyCode)) {
        if (!enabled_ || !compositionManager_.hasBufferedText()) {
            return S_FALSE;
        }
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        candidateWindow_.hide();
        return recover(
            compositionManager_.commitOriginalAndInsertLiteral(context, L' '),
            "Ctrl+Space commit raw word with smart boundary + trailing space"
        );
    }

    if (
        keyCode == VK_CAPITAL
        && plainCapsKeyPending_
    ) {
        plainCapsKeyPending_ = false;

        // Commit the word that was already being typed before changing the
        // temporary plain-CapsLock typing state.
        HRESULT capsCommit = S_FALSE;
        if (conversionActive_ || candidateSelectionActive_) {
            capsCommit = compositionManager_.commitCandidate(context, selectedCandidateIndex_);
        } else if (compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition()) {
            capsCommit = compositionManager_.commitOriginal(context);
        }

        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        candidateWindow_.hide();
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);

        if (FAILED(capsCommit) && capsCommit != S_FALSE) {
            debugLogHr("commit composition before plain CapsLock", capsCommit);
        }

        plainCapsCapitalMode_ = !plainCapsCapitalMode_;
        debugLog(plainCapsCapitalMode_
            ? "Plain CapsLock: English capital bypass ON"
            : "Plain CapsLock: English capital bypass OFF");

        // Eat this plain CapsLock inside TSF. The independent latch above is
        // what controls Myanglish bypass; Shift+CapsLock is untouched.
        return S_OK;
    }

    if (isShortcutModifierPressed()) {
        return S_FALSE;
    }

    if (!enabled_) {
        if (isModeToggle(keyCode)) {
            enabled_ = true;
            if (languageBarButton_ != nullptr) {
                languageBarButton_->notifyModeChanged();
            }
            debugLog("Switched to Myanglish mode");
            return S_OK;
        }
        return S_FALSE;
    }

    // Shift+Space = persistent Myanglish <-> English. If a raw word is being
    // typed, keep it exactly as typed before leaving Myanglish mode.
    if (isModeToggle(keyCode)) {
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        candidateWindow_.hide();
        if (compositionManager_.hasBufferedText() || compositionManager_.hasActiveComposition()) {
            const HRESULT hr = compositionManager_.commitOriginal(context);
            if (FAILED(hr) && hr != S_FALSE) {
                return recover(hr, "commit raw before English mode");
            }
        }
        enabled_ = false;
        if (languageBarButton_ != nullptr) {
            languageBarButton_->notifyModeChanged();
        }
        debugLog("Switched to English mode");
        return S_OK;
    }

    // Alpha 0.9.3: a Shift tap does nothing. We arm stack mode only when
    // the user types a Roman letter while Shift is physically held.

    if (candidateWindow_.isVisible()) {
        // alpha-0.8.8 Romaji-style Space loop:
        //   first Space converted to #1 without popup; second Space opened this
        //   popup at #2. Every following Space advances #3, #4, ... and wraps
        //   back to #1. Enter commits, or the next Roman letter auto-commits the
        //   selected candidate and starts the next raw Myanglish word.
        if (keyCode == VK_SPACE) {
            const std::size_t count = candidateWindow_.candidateCount();
            if (count == 0) {
                return S_FALSE;
            }
            const std::size_t next = (candidateWindow_.selectedIndex() + 1) % count;
            candidateWindow_.setSelection(next);
            selectedCandidateIndex_ = next;
            candidateSelectionActive_ = true;
            conversionActive_ = true;
            return recover(compositionManager_.previewCandidate(context, next), "space preview next candidate");
        }
        if (keyCode == VK_RETURN) {
            const HRESULT hr = commitSelectedCandidate(context);
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            return recover(hr, "enter commit selected candidate");
        }
        if (keyCode == VK_TAB) {
            const bool rawCandidate =
                compositionManager_.isRawCandidate(selectedCandidateIndex_);
            candidateWindow_.hide();
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            const HRESULT hr = rawCandidate
                ? compositionManager_.commitOriginalAndInsertLiteral(context, L' ')
                : compositionManager_.commitVisiblePreviewAndInsertLiteral(context, L' ');
            compositionManager_.setStackPrefixEnabled(false);
            return recover(hr, "tab accept visible candidate + real space");
        }
        if (isR111ArrowKey(keyCode)) {
            const HRESULT hr = compositionManager_.commitVisiblePreview(context);
            candidateWindow_.hide();
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            if (FAILED(hr) && hr != S_FALSE) {
                return recover(hr, "R1.1 arrow auto-commit");
            }
            return S_FALSE;
        }
        if (isCandidateNumberKey(keyCode)) {
            return recover(
                commitCandidateByNumber(context, static_cast<std::size_t>(keyCode - '1')),
                "number auto-commit candidate"
            );
        }
        if (keyCode == VK_ESCAPE || keyCode == VK_BACK) {
            // Romaji-style cancel: while converted, Esc or Backspace cancels the
            // conversion and restores the complete raw Myanglish composition.
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            candidateWindow_.hide();
            return recover(
                compositionManager_.restoreOriginalPreview(context),
                keyCode == VK_ESCAPE ? "escape cancel conversion to raw" : "backspace cancel conversion to raw"
            );
        }
        if (isAsciiLetter(keyCode)) {
            const bool shiftHeldForLetter = (GetKeyState(VK_SHIFT) < 0);
            if (candidateSelectionActive_) {
                const std::size_t selected = selectedCandidateIndex_;
                candidateWindow_.hide();
                candidateSelectionActive_ = false;
                conversionActive_ = false;

                HRESULT atomicHr = S_OK;

                if (shiftHeldForLetter) {
                    // Alpha 0.9.9: stack means JOIN, not commit+new-composition.
                    // Keep the previous candidate and the new virama-led syllable
                    // inside one TSF composition range.
                    compositionManager_.setStackPrefixEnabled(stackMode_);
                    atomicHr = compositionManager_.beginStackJoin(
                        context,
                        selected,
                        toLowerAsciiKey(keyCode)
                    );
                    if (SUCCEEDED(atomicHr)) {
                        stackMode_ = true;
                        compositionManager_.setStackPrefixEnabled(true);
                    }
                } else {
                    compositionManager_.setStackPrefixEnabled(stackMode_);
                    atomicHr = compositionManager_.commitVisiblePreviewAndStartNext(
                        context,
                        toLowerAsciiKey(keyCode)
                    );
                    if (SUCCEEDED(atomicHr)) {
                        stackMode_ = false;
                        compositionManager_.setStackPrefixEnabled(false);
                    }
                }

                selectedCandidateIndex_ = 0;

                if (SUCCEEDED(atomicHr)) {
                    refreshCandidateWindow();
                }
                return recover(
                    atomicHr,
                    shiftHeldForLetter
                        ? "join stacked syllable inside current composition"
                        : "atomic auto-commit selected candidate + start next word"
                );
            }

            if (shiftHeldForLetter) {
                stackMode_ = true;
                compositionManager_.setStackPrefixEnabled(true);
            }
            const HRESULT hr = compositionManager_.insertCharacter(context, toLowerAsciiKey(keyCode));
            if (SUCCEEDED(hr)) {
                refreshCandidateWindow();
            }
            return recover(hr, "insertCharacter current raw word");
        }
    }

    if (isAsciiLetter(keyCode)) {
        const bool shiftHeldForLetter = (GetKeyState(VK_SHIFT) < 0);

        // Lexicon Pack 2: Shift+T at the START of a fresh word is the
        // case-sensitive T shortcut, not a stack request. Shift+letters while
        // a word/candidate is already active keep the existing stack behavior.
        if (
            shiftHeldForLetter &&
            keyCode == 'T' &&
            !conversionActive_ &&
            !candidateSelectionActive_ &&
            !compositionManager_.hasBufferedText()
        ) {
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            const HRESULT shortcutHr =
                compositionManager_.insertCharacter(context, 'T');
            if (SUCCEEDED(shortcutHr)) {
                refreshCandidateWindow();
            }
            return recover(shortcutHr, "insert uppercase T shortcut");
        }

        if (conversionActive_ || candidateSelectionActive_) {
            const std::size_t selected = selectedCandidateIndex_;
            candidateWindow_.hide();
            candidateSelectionActive_ = false;
            conversionActive_ = false;

            HRESULT atomicHr = S_OK;

            if (shiftHeldForLetter) {
                // Stack the next syllable into the SAME composition.
                compositionManager_.setStackPrefixEnabled(stackMode_);
                atomicHr = compositionManager_.beginStackJoin(
                    context,
                    selected,
                    toLowerAsciiKey(keyCode)
                );
                if (SUCCEEDED(atomicHr)) {
                    stackMode_ = true;
                    compositionManager_.setStackPrefixEnabled(true);
                }
            } else {
                compositionManager_.setStackPrefixEnabled(stackMode_);
                atomicHr = compositionManager_.commitVisiblePreviewAndStartNext(
                    context,
                    toLowerAsciiKey(keyCode)
                );
                if (SUCCEEDED(atomicHr)) {
                    stackMode_ = false;
                    compositionManager_.setStackPrefixEnabled(false);
                }
            }

            selectedCandidateIndex_ = 0;

            if (SUCCEEDED(atomicHr)) {
                refreshCandidateWindow();
            }
            return recover(
                atomicHr,
                shiftHeldForLetter
                    ? "join stacked syllable inside current composition"
                    : "atomic auto-commit converted candidate + start next raw word"
            );
        }

        // Stack is armed only when Shift is physically held on the FIRST
        // Roman letter of a fresh syllable. Shift tap alone never arms it,
        // and pressing Shift later inside an ordinary word cannot leak a virama.
        if (!compositionManager_.hasBufferedText()) {
            stackMode_ = shiftHeldForLetter;
            compositionManager_.setStackPrefixEnabled(stackMode_);
        }
        const HRESULT hr = compositionManager_.insertCharacter(context, toLowerAsciiKey(keyCode));
        if (SUCCEEDED(hr)) {
            refreshCandidateWindow();
        }
        return recover(hr, "insert raw Myanglish character");
    }

    if (keyCode == VK_BACK) {
        if (compositionManager_.hasPendingRawAutoSpace()
            && !conversionActive_ && !candidateSelectionActive_
            && !compositionManager_.hasBufferedText()) {
            candidateWindow_.hide();
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            const HRESULT hr = compositionManager_.undoRawAutoSpaceAndResume(context);
            if (SUCCEEDED(hr)) {
                refreshCandidateWindow();
            }
            return recover(hr, "smart undo auto-space and resume raw composition");
        }

        if (conversionActive_ || candidateSelectionActive_) {
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            candidateWindow_.hide();
            return recover(compositionManager_.restoreOriginalPreview(context), "backspace cancel conversion to raw");
        }
        const HRESULT hr = compositionManager_.deleteBackspace(context);
        if (SUCCEEDED(hr)) {
            refreshCandidateWindow();
        }
        return recover(hr, "delete raw Myanglish character");
    }

    // D+* family: when * is typed while a Roman composition is active,
    // keep it inside the raw buffer so d* can use the normal Space candidate
    // flow (ဒီ, ဒီနေ့, ဒီည, ဒီနေရာ).
    if (isTypedStarKey(keyCode) && compositionManager_.hasBufferedText()) {
        const HRESULT hr = compositionManager_.insertCharacter(context, L'*');
        if (SUCCEEDED(hr)) {
            refreshCandidateWindow();
        }
        return recover(hr, "insert * into Myanglish composition");
    }

    // Burmese digits. Candidate-popup 1..9 handling runs earlier, so number
    // keys still select popup candidates when the popup is visible.
    if (isR111MyanmarDigitKey(keyCode)) {
        const wchar_t digit = myanmarDigitForKey(keyCode);
        if (!compositionManager_.hasBufferedText()) {
            return recover(compositionManager_.insertLiteral(context, digit), "insert Myanmar digit");
        }

        const bool converted = conversionActive_ || candidateSelectionActive_;
        const bool rawCandidate = converted
            && compositionManager_.isRawCandidate(selectedCandidateIndex_);
        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        const HRESULT hr = rawCandidate
            ? compositionManager_.commitOriginalAndInsertLiteral(context, digit)
            : (converted
                ? compositionManager_.commitVisiblePreviewAndInsertLiteral(context, digit)
                : compositionManager_.commitBestCandidateAndInsertLiteral(context, digit));
        return recover(hr, "commit composition + Myanmar digit");
    }

    // Keyboard-layout-independent semicolon KEY rule requested by the user.
    // We identify the key whose unshifted character is ';', so this works on
    // both JIS and US layouts even though Shift+; produces different symbols.
    //   ;       -> း
    //   Shift+; -> ့
    if (isSemicolonBaseKey(keyCode)) {
        const wchar_t mark = (GetKeyState(VK_SHIFT) < 0)
            ? static_cast<wchar_t>(0x1037)  // ့
            : static_cast<wchar_t>(0x1038); // း

        if (!compositionManager_.hasBufferedText()) {
            return recover(compositionManager_.insertLiteral(context, mark), "insert semicolon Myanmar mark");
        }

        const bool converted = conversionActive_ || candidateSelectionActive_;
        const bool rawCandidate = converted
            && compositionManager_.isRawCandidate(selectedCandidateIndex_);
        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        const HRESULT hr = rawCandidate
            ? compositionManager_.commitOriginalAndInsertLiteral(context, mark)
            : (converted
                ? compositionManager_.commitVisiblePreviewAndInsertLiteral(context, mark)
                : compositionManager_.commitBestCandidateAndInsertLiteral(context, mark));
        return recover(hr, "commit composition + semicolon Myanmar mark");
    }

    // R1.1 Shift+backslash (|) -> င်. Normal backslash keeps R1's င်္.
    {
        wchar_t typed = 0;
        if (translatedCharacterForKey(keyCode, typed) && typed == L'|') {
            const wchar_t ngAsat[] = {
                static_cast<wchar_t>(0x1004),
                static_cast<wchar_t>(0x103A)
            };
            candidateWindow_.hide();
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);

            HRESULT hr = compositionManager_.insertLiteral(context, ngAsat[0]);
            if (FAILED(hr)) return recover(hr, "R1.1 Shift+backslash ng");
            return recover(
                compositionManager_.insertLiteral(context, ngAsat[1]),
                "R1.1 Shift+backslash asat"
            );
        }
    }

    // Alpha 0.10.5.1: actual typed backslash inserts kinzi "င်္".
    {
        wchar_t typed = 0;
        if (translatedCharacterForKey(keyCode, typed) && typed == L'\\') {
            candidateWindow_.hide();
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);

            HRESULT hr = S_OK;
            if (compositionManager_.hasBufferedText()
                || compositionManager_.hasActiveComposition()) {
                if (conversionActive_ || candidateSelectionActive_) {
                    hr = compositionManager_.commitVisiblePreview(context);
                } else {
                    hr = compositionManager_.commitBestCandidate(context);
                }

                if (FAILED(hr) && hr != S_FALSE) {
                    return recover(hr, "commit before backslash kinzi");
                }
            }

            return recover(
                compositionManager_.beginKinziPending(context),
                "backslash begin kinzi pending"
            );
        }
    }

    // R1.1: non-custom symbols stay in the host app's English layout.
    // Finish Myanglish synchronously, then do NOT eat the key.
    if (isR111PassThroughSymbol(keyCode)) {
        HRESULT hr = S_FALSE;
        if (conversionActive_ || candidateSelectionActive_) {
            hr = compositionManager_.commitVisiblePreview(context);
        } else if (compositionManager_.hasBufferedText()
                   || compositionManager_.hasActiveComposition()) {
            hr = compositionManager_.commitBestCandidate(context);
        }
        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        if (FAILED(hr) && hr != S_FALSE) {
            return recover(hr, "R1.1 pass-through English symbol");
        }
        return S_FALSE;
    }

    if (isMyanmarPunctuationKey(keyCode)) {
        wchar_t typedPunctuation = 0;
        if (!translatedCharacterForKey(keyCode, typedPunctuation)) {
            return S_FALSE;
        }
        const wchar_t punctuation = punctuationForCharacter(typedPunctuation, smartQuoteOpen_);
        if (!compositionManager_.hasBufferedText()) {
            candidateWindow_.hide();
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            return recover(
                compositionManager_.insertLiteral(context, punctuation),
                "insert layout-aware Myanmar punctuation"
            );
        }
        const bool converted = conversionActive_ || candidateSelectionActive_;
        const bool rawCandidate = converted
            && compositionManager_.isRawCandidate(selectedCandidateIndex_);
        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        const HRESULT hr = rawCandidate
            ? compositionManager_.commitOriginalAndInsertLiteral(context, punctuation)
            : (converted
                ? compositionManager_.commitVisiblePreviewAndInsertLiteral(context, punctuation)
                : compositionManager_.commitBestCandidateAndInsertLiteral(context, punctuation));
        compositionManager_.setStackPrefixEnabled(false);
        return recover(hr, "commit + layout-aware Myanmar punctuation");
    }

    if (keyCode == VK_SPACE) {
        if (!compositionManager_.hasBufferedText()) {
            return S_FALSE;
        }

        const auto candidates = compositionManager_.currentCandidateTexts(9);
        if (candidates.empty()) {
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            candidateWindow_.hide();
            stackMode_ = false;
            compositionManager_.setStackPrefixEnabled(false);
            return recover(
                compositionManager_.commitOriginalAndInsertLiteral(context, L' '),
                "unknown word Space: commit raw + insert space"
            );
        }

        if (!conversionActive_) {
            // FIRST Space always previews candidate #1. When the setting is ON
            // (the default), open the complete candidate popup immediately with
            // #1 selected. When OFF, preserve the legacy hidden-first-Space flow.
            conversionActive_ = true;
            candidateSelectionActive_ = true;
            selectedCandidateIndex_ = 0;

            const HRESULT previewHr = compositionManager_.previewCandidate(context, 0);
            if (FAILED(previewHr)) {
                return recover(previewHr, "first space preview candidate #1");
            }

            if (liveCandidatesEnabled_) {
                if (!openCandidateWindow()) {
                    candidateWindow_.hide();
                } else {
                    candidateWindow_.setSelection(0);
                }
                return S_OK;
            }

            candidateWindow_.hide();
            return S_OK;
        }

        // Legacy OFF mode: SECOND Space opens the popup at #2. In the default
        // ON mode the popup is already visible, so this block is not reached:
        // the visible-popup Space handler above advances #1 -> #2 -> #3...
        if (!openCandidateWindow()) {
            return S_FALSE;
        }
        const std::size_t count = candidateWindow_.candidateCount();
        selectedCandidateIndex_ = count > 1 ? 1 : 0;
        candidateWindow_.setSelection(selectedCandidateIndex_);
        candidateSelectionActive_ = true;
        conversionActive_ = true;
        return recover(
            compositionManager_.previewCandidate(context, selectedCandidateIndex_),
            "legacy second space open popup and select candidate #2"
        );
    }

    if (keyCode == VK_TAB) {
        if (!compositionManager_.hasBufferedText()) {
            return S_FALSE;
        }
        const bool converted = conversionActive_ || candidateSelectionActive_;
        const bool rawCandidate = converted
            && compositionManager_.isRawCandidate(selectedCandidateIndex_);
        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        const HRESULT hr = rawCandidate
            ? compositionManager_.commitOriginalAndInsertLiteral(context, L' ')
            : (converted
                ? compositionManager_.commitVisiblePreviewAndInsertLiteral(context, L' ')
                : compositionManager_.commitBestCandidateAndInsertLiteral(context, L' '));
        compositionManager_.setStackPrefixEnabled(false);
        return recover(hr, "tab commit composition + real space");
    }

    if (keyCode == VK_LEFT || keyCode == VK_RIGHT) {
        // R1.2c: Left/Right are navigation keys, not candidate-selection keys.
        // Commit exactly what is currently visible, clear IME state, then return
        // S_FALSE so the host application performs the actual caret movement.
        HRESULT hr = S_FALSE;

        if (conversionActive_ || candidateSelectionActive_) {
            hr = compositionManager_.commitVisiblePreview(context);
        } else if (
            compositionManager_.hasBufferedText()
            || compositionManager_.hasActiveComposition()
        ) {
            hr = compositionManager_.commitOriginal(context);
        }

        candidateWindow_.hide();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);

        if (FAILED(hr) && hr != S_FALSE) {
            return recover(hr, "R1.2c Left/Right auto-commit");
        }

        return S_FALSE;
    }

    if (keyCode == VK_DOWN || keyCode == VK_UP) {
        if (!openCandidateWindow()) {
            return S_FALSE;
        }
        candidateSelectionActive_ = true;
        conversionActive_ = true;
        selectedCandidateIndex_ = 0;
        const int delta = keyCode == VK_UP ? -1 : +1;
        const HRESULT moveHr = moveCandidateSelection(delta);
        if (FAILED(moveHr) || moveHr == S_FALSE) {
            return moveHr;
        }
        return recover(
            compositionManager_.previewCandidate(context, selectedCandidateIndex_),
            "open and preview arrow/tab candidate"
        );
    }

    if (keyCode == VK_RETURN) {
        candidateWindow_.hide();
        if (!compositionManager_.hasBufferedText()) {
            return S_FALSE;
        }
        const HRESULT hr = (conversionActive_ || candidateSelectionActive_)
            ? compositionManager_.commitVisiblePreview(context)
            : compositionManager_.commitOriginal(context);
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        return recover(hr, "enter commit current composition");
    }

    if (keyCode == VK_ESCAPE) {
        candidateWindow_.hide();
        if (conversionActive_ || candidateSelectionActive_) {
            candidateSelectionActive_ = false;
            conversionActive_ = false;
            selectedCandidateIndex_ = 0;
            return recover(compositionManager_.restoreOriginalPreview(context), "escape cancel conversion to raw");
        }
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        return compositionManager_.cancel(context);
    }

    return S_FALSE;
}

HRESULT TextService::onSetFocus(BOOL foreground) {
    if (!foreground) {
        candidateWindow_.hide();
        (void)commitVisibleOnFocusLoss();
        candidateSelectionActive_ = false;
        conversionActive_ = false;
        selectedCandidateIndex_ = 0;
        stackMode_ = false;
        compositionManager_.setStackPrefixEnabled(false);
        releaseRememberedContext();
        debugLog("KeyEventSink focus lost; popup/conversion state force-cleared and visible word finalized");
    } else {
        debugLog("KeyEventSink focus gained");
    }
    return S_OK;
}

} // namespace myanglish::ime
