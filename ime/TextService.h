#pragma once

#include "CandidateWindow.h"
#include "CompositionManager.h"
#include "Globals.h"

#include <atomic>
#include <cstddef>

#include <Windows.h>
#include <msctf.h>
#include <ctfutb.h>

namespace myanglish::ime {

class KeyEventSink;
class LanguageBarButton;

class TextService final : public ITfTextInputProcessorEx, public ITfDisplayAttributeProvider {
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
    HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** enumInfo) override;
    HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** info) override;

    bool shouldHandleKeyDown(ITfContext* context, WPARAM keyCode) const noexcept;
    HRESULT processKeyDown(ITfContext* context, WPARAM keyCode);
    HRESULT onSetFocus(BOOL foreground);
    bool isMyanglishModeEnabled() const noexcept { return enabled_; }
    HRESULT toggleModeFromLanguageBar() noexcept;

private:
    HRESULT activateInternal(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags);
    HRESULT deactivateInternal();

    bool openCandidateWindow();
    void refreshCandidateWindow();
    HRESULT moveCandidateSelection(int delta);
    HRESULT commitSelectedCandidate(ITfContext* context);
    HRESULT commitCandidateByNumber(ITfContext* context, std::size_t index);

    void rememberContext(ITfContext* context) noexcept;
    void releaseRememberedContext() noexcept;
    HRESULT commitVisibleOnFocusLoss() noexcept;
    void handleCandidateForegroundLoss() noexcept;
    void handleCandidateMouseSelection(std::size_t index) noexcept;
    void handleCandidateMouseCommit(std::size_t index) noexcept;
    void handleManageWordsRequested() noexcept;

    static bool isShortcutModifierPressed() noexcept;
    static bool isAsciiLetter(WPARAM keyCode) noexcept;
    static bool isCandidateNumberKey(WPARAM keyCode) noexcept;
    static bool isHostEditingShortcut(WPARAM keyCode) noexcept;
    static wchar_t toLowerAsciiKey(WPARAM keyCode) noexcept;

    std::atomic<ULONG> refCount_{1};
    ITfThreadMgr* threadMgr_ = nullptr;
    ITfKeystrokeMgr* keystrokeMgr_ = nullptr;
    ITfLangBarItemMgr* languageBarItemMgr_ = nullptr;
    ITfContext* lastContext_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    bool active_ = false;
    bool enabled_ = true;
    // Tracks plain CapsLock independently of TSF/JIS GetKeyState timing.
    // Shift+CapsLock remains the R1.14 persistent language-mode shortcut.
    mutable bool plainCapsCapitalMode_ = false;
    mutable bool plainCapsKeyPending_ = false;
    bool liveCandidatesEnabled_ = true;
    bool candidateSelectionActive_ = false;
    bool conversionActive_ = false;
    bool stackMode_ = false;
    mutable bool stackShortcutPending_ = false;
    bool smartQuoteOpen_ = true;
    std::size_t selectedCandidateIndex_ = 0;
    CompositionManager compositionManager_;
    CandidateWindow candidateWindow_;
    KeyEventSink* keyEventSink_ = nullptr;
    LanguageBarButton* languageBarButton_ = nullptr;
};

} // namespace myanglish::ime
