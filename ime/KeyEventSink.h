#pragma once

#include <atomic>

#include <Windows.h>
#include <msctf.h>

namespace myanglish::ime {

class TextService;

class KeyEventSink final : public ITfKeyEventSink {
public:
    explicit KeyEventSink(TextService& service);
    ~KeyEventSink();

    KeyEventSink(const KeyEventSink&) = delete;
    KeyEventSink& operator=(const KeyEventSink&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL fForeground) override;
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

private:
    TextService& service_;
    std::atomic<ULONG> refCount_{1};
};

} // namespace myanglish::ime