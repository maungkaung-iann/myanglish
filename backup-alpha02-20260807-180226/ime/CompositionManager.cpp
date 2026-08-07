#include "CompositionManager.h"

#include "Dictionary.h"
#include "Guids.h"
#include "MyanglishConverter.h"

#include <new>
#include <utility>

namespace myanglish::ime {

namespace {

class CompositionSink final : public ITfCompositionSink {
public:
    CompositionSink() noexcept = default;

    STDMETHODIMP QueryInterface(
        REFIID riid,
        void** object
    ) override {
        if (object == nullptr) {
            return E_POINTER;
        }

        *object = nullptr;

        if (riid == IID_IUnknown || riid == IID_ITfCompositionSink) {
            *object = static_cast<ITfCompositionSink*>(this);
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

    STDMETHODIMP OnCompositionTerminated(
        TfEditCookie,
        ITfComposition*
    ) override {
        debugLog("Composition terminated by TSF");
        return S_OK;
    }

private:
    ~CompositionSink() = default;
    LONG referenceCount_{1};
};

} // namespace

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
        dataRoot_ / "data" / "dictionary.csv";

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
        debugLogHr("RequestEditSession", requestResult);
        return requestResult;
    }

    if (FAILED(editSessionResult)) {
        debugLogHr("DoEditSession", editSessionResult);
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

            // Do not log user text. The debug log records lifecycle and errors
            // only, so it is safe to leave enabled during alpha testing.
            debugLog("Composition character inserted");
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
            // Remove the final visible composition character before ending the
            // composition. Ending alone would leave that character committed.
            const HRESULT clearResult =
                updateCompositionText(editCookie, context, L"");

            if (FAILED(clearResult)) {
                return clearResult;
            }

            const HRESULT endResult = endComposition(editCookie);
            if (SUCCEEDED(endResult)) {
                buffer_.clear();
            }

            return endResult;
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

    // Do not use ITfContext::GetSelection() directly as the composition
    // range.  For a text service, TSF provides ITfInsertAtSelection so we
    // can ask for the exact insertion range the context owner accepts.
    // This is the same pattern used by established TSF IMEs.
    ITfInsertAtSelection* insertAtSelection = nullptr;
    HRESULT result = context->QueryInterface(
        IID_ITfInsertAtSelection,
        reinterpret_cast<void**>(&insertAtSelection)
    );

    if (FAILED(result) || insertAtSelection == nullptr) {
        const HRESULT interfaceResult = FAILED(result) ? result : E_NOINTERFACE;
        debugLogHr("QueryInterface(ITfInsertAtSelection)", interfaceResult);
        if (insertAtSelection != nullptr) {
            insertAtSelection->Release();
        }
        return interfaceResult;
    }

    ITfRange* insertionRange = nullptr;
    result = insertAtSelection->InsertTextAtSelection(
        editCookie,
        TF_IAS_QUERYONLY,
        nullptr,
        0,
        &insertionRange
    );

    insertAtSelection->Release();
    insertAtSelection = nullptr;

    if (FAILED(result) || insertionRange == nullptr) {
        const HRESULT insertResult = FAILED(result) ? result : E_FAIL;
        debugLogHr("InsertTextAtSelection(TF_IAS_QUERYONLY)", insertResult);
        if (insertionRange != nullptr) {
            insertionRange->Release();
        }
        return insertResult;
    }

    ITfContextComposition* contextComposition = nullptr;
    result = context->QueryInterface(
        IID_ITfContextComposition,
        reinterpret_cast<void**>(&contextComposition)
    );

    if (FAILED(result) || contextComposition == nullptr) {
        const HRESULT interfaceResult = FAILED(result) ? result : E_NOINTERFACE;
        debugLogHr("QueryInterface(ITfContextComposition)", interfaceResult);
        insertionRange->Release();
        if (contextComposition != nullptr) {
            contextComposition->Release();
        }
        return interfaceResult;
    }

    CompositionSink* compositionSink =
        new (std::nothrow) CompositionSink();

    if (compositionSink == nullptr) {
        insertionRange->Release();
        contextComposition->Release();
        return E_OUTOFMEMORY;
    }

    result = contextComposition->StartComposition(
        editCookie,
        insertionRange,
        compositionSink,
        &composition_
    );

    // StartComposition keeps its own reference to the sink on success.
    compositionSink->Release();
    compositionSink = nullptr;

    insertionRange->Release();
    insertionRange = nullptr;
    contextComposition->Release();
    contextComposition = nullptr;

    if (FAILED(result)) {
        debugLogHr("StartComposition", result);
        if (composition_ != nullptr) {
            composition_->Release();
            composition_ = nullptr;
        }
        return result;
    }

    if (composition_ == nullptr) {
        // S_OK + nullptr means the context owner rejected the composition.
        debugLog("StartComposition returned S_OK but the context owner rejected the composition");
        return E_FAIL;
    }

    debugLog("Composition started");
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
        debugLogHr("ensureComposition", compositionResult);
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

        const HRESULT rangeResult = FAILED(result) ? result : E_FAIL;
        debugLogHr("ITfComposition::GetRange", rangeResult);
        return rangeResult;
    }

    // This is normal IME composition text creation/replacement, not a
    // correction of already committed document text. TF_ST_CORRECTION is
    // intended for corrections of existing content and can be rejected with
    // E_INVALIDARG when used for a new composition insertion.
    result =
        range->SetText(
            editCookie,
            0,
            text.c_str(),
            static_cast<LONG>(text.size())
        );

    if (FAILED(result)) {
        debugLogHr("ITfRange::SetText", result);
    }

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