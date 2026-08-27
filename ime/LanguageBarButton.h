#pragma once

#include <Windows.h>
#include <ctfutb.h>
#include <ctffunc.h>
#include <msctf.h>
#include <ocidl.h>

#include <atomic>

namespace myanglish::ime {

class TextService;

class LanguageBarButton final :
    public ITfLangBarItemButton,
    public ITfSource {
public:
    explicit LanguageBarButton(TextService& service);
    ~LanguageBarButton();

    LanguageBarButton(const LanguageBarButton&) = delete;
    LanguageBarButton& operator=(const LanguageBarButton&) = delete;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // ITfLangBarItem
    HRESULT STDMETHODCALLTYPE GetInfo(TF_LANGBARITEMINFO* info) override;
    HRESULT STDMETHODCALLTYPE GetStatus(DWORD* status) override;
    HRESULT STDMETHODCALLTYPE Show(BOOL show) override;
    HRESULT STDMETHODCALLTYPE GetTooltipString(BSTR* tooltip) override;

    // ITfLangBarItemButton
    HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick click, POINT point, const RECT* area) override;
    HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu* menu) override;
    HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT id) override;
    HRESULT STDMETHODCALLTYPE GetIcon(HICON* icon) override;
    HRESULT STDMETHODCALLTYPE GetText(BSTR* text) override;

    // ITfSource
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID riid, IUnknown* punk, DWORD* cookie) override;
    HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override;

    void notifyModeChanged() noexcept;

private:
    std::atomic<ULONG> refCount_{1};
    TextService& service_;
    ITfLangBarItemSink* sink_ = nullptr;
    DWORD sinkCookie_ = TF_INVALID_COOKIE;
    bool shown_ = true;
};

} // namespace myanglish::ime
