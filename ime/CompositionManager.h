#pragma once

#include "Globals.h"

#include <memory>
#include <string>

#include <Windows.h>
#include <msctf.h>

namespace myanglish {
class MyanglishConverter;
}

namespace myanglish::ime {

class CompositionManager {
public:
    explicit CompositionManager(const std::filesystem::path& dataRoot = {});
    ~CompositionManager();

    CompositionManager(const CompositionManager&) = delete;
    CompositionManager& operator=(const CompositionManager&) = delete;

    bool isReady() const noexcept;
    bool hasBufferedText() const noexcept;
    bool hasActiveComposition() const noexcept;
    void setClientId(TfClientId clientId) noexcept;

    HRESULT insertCharacter(ITfContext* context, wchar_t character);
    HRESULT deleteBackspace(ITfContext* context);
    HRESULT commitOriginal(ITfContext* context);
    HRESULT commitBestCandidate(ITfContext* context);
    HRESULT cancel(ITfContext* context);
    void clearWithoutContext() noexcept;

private:
    class EditSession;

public:
    enum class EditAction {
        InsertCharacter,
        DeleteBackspace,
        CommitOriginal,
        CommitBestCandidate,
        Cancel,
    };

    HRESULT executeEdit(TfEditCookie ec, ITfContext* context, EditAction action, wchar_t character);

    HRESULT requestEdit(ITfContext* context, EditAction action, wchar_t character = 0);

    HRESULT ensureComposition(TfEditCookie ec, ITfContext* context);
    HRESULT updateCompositionText(TfEditCookie ec, ITfContext* context, const std::wstring& text);
    HRESULT endComposition(TfEditCookie ec);

    std::wstring makeCommittedText(bool useBestCandidate) const;

    std::filesystem::path dataRoot_;
    std::unique_ptr<myanglish::MyanglishConverter> converter_;
    std::string buffer_;
    ITfComposition* composition_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
};

} // namespace myanglish::ime