#include "CompositionManager.h"

#include "Dictionary.h"
#include "Guids.h"
#include "MyanglishConverter.h"

#include <new>
#include <utility>

namespace myanglish::ime {

class CompositionManager::EditSession final : public ITfEditSession {
public:
    EditSession(
        CompositionManager* manager,
        ITfContext* context,
        EditAction action,
        wchar_t character
    )
        : referenceCount_(1),
          manager_(manager),
          context_(context),
          action_(action),
          character_(character) {
        if (context_ != nullptr) {
            context_->AddRef();
        }
    }

    ~EditSession() {
        if (context_ != nullptr) {
            context_->Release();
            context_ = nullptr;
        }
    }

    STDMETHODIMP QueryInterface(
        REFIID riid,
        void** object
    ) override {
        if (object == nullptr) {
            return E_POINTER;
        }

        *object = nullptr;

        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *object = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(
            InterlockedIncrement(&referenceCount_)
        );
    }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG count =
            InterlockedDecrement(&referenceCount_);

        if (count == 0) {
            delete this;
        }

        return static_cast<ULONG>(count);
    }

    STDMETHODIMP DoEditSession(
        TfEditCookie editCookie
    ) override {
        if (manager_ == nullptr || context_ == nullptr) {
            return E_FAIL;
        }

        return manager_->executeEdit(
            editCookie,
            context_,
            action_,
            character_
        );
    }

private:
    LONG referenceCount_;
    CompositionManager* manager_;
    ITfContext* context_;
    EditAction action_;
    wchar_t character_;
};

CompositionManager::CompositionManager(
    const std::filesystem::path& dataRoot
)
    : dataRoot_(
          dataRoot.empty()
              ? resolveDataRoot()
              : dataRoot
      ) {
    myanglish::Dictionary dictionary;
    std::string errorMessage;

    const std::filesystem::path dictionaryFile =
        dataRoot_ / "dictionary.csv";

    if (
        dictionary.loadFromCsv(
            dictionaryFile,
            &errorMessage
        )
    ) {
        converter_ =
            std::make_unique<myanglish::MyanglishConverter>(
                std::move(dictionary),
                dataRoot_
            );

        debugLog(
            std::string("Dictionary loaded: ") +
            dictionaryFile.string()
        );
    } else {
        debugLog(
            std::string("Dictionary load failed: ") +
            errorMessage
        );
    }
}

CompositionManager::~CompositionManager() {
    if (composition_ != nullptr) {
        composition_->Release();
        composition_ = nullptr;
    }
}

bool CompositionManager::isReady() const noexcept {
    return converter_ != nullptr;
}

bool CompositionManager::hasBufferedText() const noexcept {
    return !buffer_.empty();
}

bool CompositionManager::hasActiveComposition() const noexcept {
    return composition_ != nullptr;
}

void CompositionManager::setClientId(
    TfClientId clientId
) noexcept {
    clientId_ = clientId;
}

HRESULT CompositionManager::insertCharacter(
    ITfContext* context,
    wchar_t character
) {
    return requestEdit(
        context,
        EditAction::InsertCharacter,
        character
    );
}

HRESULT CompositionManager::deleteBackspace(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::DeleteBackspace
    );
}

HRESULT CompositionManager::commitOriginal(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::CommitOriginal
    );
}

HRESULT CompositionManager::commitBestCandidate(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::CommitBestCandidate
    );
}

HRESULT CompositionManager::cancel(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::Cancel
    );
}

void CompositionManager::clearWithoutContext() noexcept {
    buffer_.clear();

    if (composition_ != nullptr) {
        composition_->Release();
        composition_ = nullptr;
    }
}

HRESULT CompositionManager::requestEdit(
    ITfContext* context,
    EditAction action,
    wchar_t character
) {
    if (context == nullptr) {
        return E_POINTER;
    }

    if (clientId_ == TF_CLIENTID_NULL) {
        return E_UNEXPECTED;
    }

    EditSession* session =
        new (std::nothrow) EditSession(
            this,
            context,
            action,
            character
        );

    if (session == nullptr) {
        return E_OUTOFMEMORY;
    }

    HRESULT editSessionResult = E_FAIL;

    const HRESULT requestResult =
        context->RequestEditSession(
            clientId_,
            static_cast<ITfEditSession*>(session),
            TF_ES_SYNC | TF_ES_READWRITE,
            &editSessionResult
        );

    session->Release();

    if (FAILED(requestResult)) {
        debugLog("RequestEditSession failed.");
        return requestResult;
    }

    return editSessionResult;
}

HRESULT CompositionManager::executeEdit(
    TfEditCookie editCookie,
    ITfContext* context,
    EditAction action,
    wchar_t character
) {
    if (context == nullptr) {
        return E_POINTER;
    }

    switch (action) {
    case EditAction::InsertCharacter: {
        if (
            character < L'A' ||
            (
                character > L'Z' &&
                character < L'a'
            ) ||
            character > L'z'
        ) {
            return E_INVALIDARG;
        }

        char asciiCharacter =
            static_cast<char>(character);

        if (
            asciiCharacter >= 'A' &&
            asciiCharacter <= 'Z'
        ) {
            asciiCharacter =
                static_cast<char>(
                    asciiCharacter - 'A' + 'a'
                );
        }

        std::string nextBuffer = buffer_;
        nextBuffer.push_back(asciiCharacter);

        const std::wstring nextText =
            utf8ToUtf16(nextBuffer);

        if (
            nextText.empty() &&
            !nextBuffer.empty()
        ) {
            return E_FAIL;
        }

        const HRESULT result =
            updateCompositionText(
                editCookie,
                context,
                nextText
            );

        if (SUCCEEDED(result)) {
            buffer_ = std::move(nextBuffer);

            debugLog(
                std::string("Composition buffer: ") +
                buffer_
            );
        }

        return result;
    }

    case EditAction::DeleteBackspace: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        std::string nextBuffer = buffer_;
        nextBuffer.pop_back();

        if (nextBuffer.empty()) {
            const HRESULT result =
                endComposition(editCookie);

            if (SUCCEEDED(result)) {
                buffer_.clear();
            }

            return result;
        }

        const std::wstring nextText =
            utf8ToUtf16(nextBuffer);

        if (nextText.empty()) {
            return E_FAIL;
        }

        const HRESULT result =
            updateCompositionText(
                editCookie,
                context,
                nextText
            );

        if (SUCCEEDED(result)) {
            buffer_ = std::move(nextBuffer);
        }

        return result;
    }

    case EditAction::CommitOriginal:
    case EditAction::CommitBestCandidate: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        const bool useBestCandidate =
            action ==
            EditAction::CommitBestCandidate;

        const std::wstring committedText =
            makeCommittedText(useBestCandidate);

        if (committedText.empty()) {
            return E_FAIL;
        }

        const HRESULT updateResult =
            updateCompositionText(
                editCookie,
                context,
                committedText
            );

        if (FAILED(updateResult)) {
            return updateResult;
        }

        const HRESULT endResult =
            endComposition(editCookie);

        if (SUCCEEDED(endResult)) {
            buffer_.clear();
            debugLog("Composition committed.");
        }

        return endResult;
    }

    case EditAction::Cancel: {
        if (
            buffer_.empty() &&
            composition_ == nullptr
        ) {
            return S_FALSE;
        }

        if (composition_ != nullptr) {
            const HRESULT updateResult =
                updateCompositionText(
                    editCookie,
                    context,
                    L""
                );

            if (FAILED(updateResult)) {
                return updateResult;
            }
        }

        const HRESULT endResult =
            endComposition(editCookie);

        if (SUCCEEDED(endResult)) {
            buffer_.clear();
            debugLog("Composition cancelled.");
        }

        return endResult;
    }
    }

    return E_FAIL;
}

HRESULT CompositionManager::ensureComposition(
    TfEditCookie editCookie,
    ITfContext* context
) {
    if (composition_ != nullptr) {
        return S_OK;
    }

    if (context == nullptr) {
        return E_POINTER;
    }

    ITfContextComposition* contextComposition =
        nullptr;

    HRESULT result =
        context->QueryInterface(
            IID_ITfContextComposition,
            reinterpret_cast<void**>(
                &contextComposition
            )
        );

    if (FAILED(result)) {
        return result;
    }

    TF_SELECTION selection = {};
    ULONG fetched = 0;

    result =
        context->GetSelection(
            editCookie,
            TF_DEFAULT_SELECTION,
            1,
            &selection,
            &fetched
        );

    if (
        SUCCEEDED(result) &&
        fetched == 1 &&
        selection.range != nullptr
    ) {
        result =
            contextComposition->StartComposition(
                editCookie,
                selection.range,
                nullptr,
                &composition_
            );
    }

    if (selection.range != nullptr) {
        selection.range->Release();
        selection.range = nullptr;
    }

    contextComposition->Release();

    if (
        FAILED(result) ||
        composition_ == nullptr
    ) {
        if (composition_ != nullptr) {
            composition_->Release();
            composition_ = nullptr;
        }

        return FAILED(result)
            ? result
            : E_FAIL;
    }

    return S_OK;
}

HRESULT CompositionManager::updateCompositionText(
    TfEditCookie editCookie,
    ITfContext* context,
    const std::wstring& text
) {
    const HRESULT compositionResult =
        ensureComposition(
            editCookie,
            context
        );

    if (FAILED(compositionResult)) {
        return compositionResult;
    }

    ITfRange* range = nullptr;

    HRESULT result =
        composition_->GetRange(&range);

    if (
        FAILED(result) ||
        range == nullptr
    ) {
        if (range != nullptr) {
            range->Release();
        }

        return FAILED(result)
            ? result
            : E_FAIL;
    }

    result =
        range->SetText(
            editCookie,
            TF_ST_CORRECTION,
            text.c_str(),
            static_cast<LONG>(text.size())
        );

    range->Release();

    return result;
}

HRESULT CompositionManager::endComposition(
    TfEditCookie editCookie
) {
    if (composition_ == nullptr) {
        return S_OK;
    }

    const HRESULT result =
        composition_->EndComposition(
            editCookie
        );

    composition_->Release();
    composition_ = nullptr;

    return result;
}

std::wstring CompositionManager::makeCommittedText(
    bool useBestCandidate
) const {
    if (buffer_.empty()) {
        return {};
    }

    if (
        useBestCandidate &&
        converter_ != nullptr
    ) {
        const auto candidates =
            converter_->getCandidates(
                buffer_,
                1
            );

        if (
            !candidates.empty() &&
            !candidates.front().burmese.empty()
        ) {
            const std::wstring converted =
                utf8ToUtf16(
                    candidates.front().burmese
                );

            if (!converted.empty()) {
                return converted;
            }
        }
    }

    return utf8ToUtf16(buffer_);
}

} // namespace myanglish::ime