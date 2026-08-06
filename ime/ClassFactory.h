#pragma once

#include <atomic>

#include <Windows.h>
#include <Unknwn.h>

namespace myanglish::ime {

class ClassFactory final : public IClassFactory {
public:
    ClassFactory();
    ~ClassFactory();

    ClassFactory(const ClassFactory&) = delete;
    ClassFactory& operator=(const ClassFactory&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppvObject) override;
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override;

private:
    std::atomic<ULONG> refCount_{1};
};

} // namespace myanglish::ime