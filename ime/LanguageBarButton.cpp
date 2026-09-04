#include "LanguageBarButton.h"

#include "Globals.h"
#include "TextService.h"

#include <OleAuto.h>
#include <cwchar>

namespace myanglish::ime {

namespace {

// SDK/header combinations differ on whether CONNECT_E_* names are surfaced
// while compiling a TSF DLL. Keep the COM-defined HRESULT values local so
// LanguageBarButton does not depend on those macro declarations.
constexpr HRESULT kConnectENoConnection =
    static_cast<HRESULT>(0x80040200UL);
constexpr HRESULT kConnectEAdviseLimit =
    static_cast<HRESULT>(0x80040201UL);
constexpr HRESULT kConnectECannotConnect =
    static_cast<HRESULT>(0x80040202UL);

// Existing Myanglish text-service CLSID used by the current project/registry.
const GUID kMyanglishTextServiceClsid =
{ 0x5f8c4d13, 0x9e7e, 0x4e5b, { 0x8c, 0x38, 0x12, 0x31, 0x59, 0x87, 0x2a, 0x10 } };

}

LanguageBarButton::LanguageBarButton(TextService& service)
    : service_(service) {
    addObject();
}

LanguageBarButton::~LanguageBarButton() {
    if (sink_ != nullptr) {
        sink_->Release();
        sink_ = nullptr;
    }
    releaseObject();
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::QueryInterface(
    REFIID riid,
    void** ppvObject
) {
    if (ppvObject == nullptr) return E_POINTER;
    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton) {
        *ppvObject = static_cast<ITfLangBarItemButton*>(this);
    } else if (riid == IID_ITfSource) {
        *ppvObject = static_cast<ITfSource*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE LanguageBarButton::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE LanguageBarButton::Release() {
    const ULONG count = --refCount_;
    if (count == 0) delete this;
    return count;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
    if (info == nullptr) return E_POINTER;

    *info = {};
    info->clsidService = kMyanglishTextServiceClsid;

    // Windows 8+ surfaces only the first input-mode language-bar item.
    // Use the system-defined input-mode GUID so this appears with the IME indicator.
    info->guidItem = GUID_LBI_INPUTMODE;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON;
    info->ulSort = 0;

    const wchar_t description[] = L"Myanglish English / Myanmar switch";
    wcsncpy_s(
        info->szDescription,
        _countof(info->szDescription),
        description,
        _TRUNCATE
    );

    return S_OK;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::GetStatus(DWORD* status) {
    if (status == nullptr) return E_POINTER;
    *status = shown_ ? 0 : TF_LBI_STATUS_HIDDEN;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::Show(BOOL show) {
    shown_ = show != FALSE;
    if (sink_ != nullptr) {
        sink_->OnUpdate(TF_LBI_STATUS);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::GetTooltipString(BSTR* tooltip) {
    if (tooltip == nullptr) return E_POINTER;
    const wchar_t* value = service_.isMyanglishModeEnabled()
        ? L"Myanglish: မြန် — click for English"
        : L"English: A — click for Myanglish";
    *tooltip = SysAllocString(value);
    return *tooltip != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::OnClick(
    TfLBIClick,
    POINT,
    const RECT*
) {
    return service_.toggleModeFromLanguageBar();
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::InitMenu(ITfMenu*) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::OnMenuSelect(UINT) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::GetIcon(HICON* icon) {
    if (icon == nullptr) return E_POINTER;
    *icon = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::GetText(BSTR* text) {
    if (text == nullptr) return E_POINTER;
    *text = SysAllocString(
        service_.isMyanglishModeEnabled() ? L"မြန်" : L"A"
    );
    return *text != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::AdviseSink(
    REFIID riid,
    IUnknown* punk,
    DWORD* cookie
) {
    if (cookie == nullptr || punk == nullptr) return E_INVALIDARG;
    if (riid != IID_ITfLangBarItemSink) return kConnectECannotConnect;
    if (sink_ != nullptr) return kConnectEAdviseLimit;

    ITfLangBarItemSink* sink = nullptr;
    const HRESULT hr = punk->QueryInterface(
        IID_ITfLangBarItemSink,
        reinterpret_cast<void**>(&sink)
    );
    if (FAILED(hr)) return hr;

    sink_ = sink;
    sinkCookie_ = 1;
    *cookie = sinkCookie_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE LanguageBarButton::UnadviseSink(DWORD cookie) {
    if (sink_ == nullptr || cookie != sinkCookie_) {
        return kConnectENoConnection;
    }

    sink_->Release();
    sink_ = nullptr;
    sinkCookie_ = TF_INVALID_COOKIE;
    return S_OK;
}

void LanguageBarButton::notifyModeChanged() noexcept {
    if (sink_ != nullptr) {
        (void)sink_->OnUpdate(TF_LBI_TEXT | TF_LBI_TOOLTIP);
    }
}

} // namespace myanglish::ime
