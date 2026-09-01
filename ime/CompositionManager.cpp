#include "CompositionManager.h"
#include "Guids.h"
#include "../engine/UnicodeUtils.h"

#include "Dictionary.h"
#include "Guids.h"
#include "MyanglishConverter.h"

#include <algorithm>
#include <fstream>
#include <new>
#include <sstream>
#include <ctime>
#include <system_error>
#include <utility>

namespace myanglish::ime {

namespace {

TfGuidAtom displayAttributeAtom(const GUID& guid) {
    ITfCategoryMgr* mgr=nullptr;
    HRESULT hr=CoCreateInstance(
        CLSID_TF_CategoryMgr,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr,
        reinterpret_cast<void**>(&mgr)
    );

    if(FAILED(hr)||!mgr) return TF_INVALID_GUIDATOM;

    TfGuidAtom atom=TF_INVALID_GUIDATOM;
    hr=mgr->RegisterGUID(guid,&atom);
    mgr->Release();

    return SUCCEEDED(hr) ? atom : TF_INVALID_GUIDATOM;
}

TfGuidAtom candidateIndicatorAtom(bool candidateAvailable) {
    static TfGuidAtom candidateAtom=TF_INVALID_GUIDATOM;
    static TfGuidAtom noCandidateAtom=TF_INVALID_GUIDATOM;

    TfGuidAtom& cached =
        candidateAvailable ? candidateAtom : noCandidateAtom;

    if(cached!=TF_INVALID_GUIDATOM) return cached;

    cached=displayAttributeAtom(
        candidateAvailable
            ? GUID_MyanglishCandidateAvailable
            : GUID_MyanglishNoCandidate
    );

    return cached;
}

bool isAutoSpacedMyanmarPunctuation(wchar_t character) {
    return character == static_cast<wchar_t>(0x104A)  // ၊
        || character == static_cast<wchar_t>(0x104B); // ။
}

void appendLiteralWithAutoSpace(
    std::wstring& text,
    wchar_t character
) {
    text.push_back(character);
    if (isAutoSpacedMyanmarPunctuation(character)) {
        text.push_back(L' ');
    }
}


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
        // The manager intentionally does not keep a raw callback pointer here.
        // If a host terminates a composition externally, the next GetRange()
        // detects it and the manager safely discards its own COM reference.
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
        wchar_t character,
        std::size_t candidateIndex
    )
        : referenceCount_(1),
          manager_(manager),
          context_(context),
          action_(action),
          character_(character),
          candidateIndex_(candidateIndex) {
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
            character_,
            candidateIndex_
        );
    }

private:
    LONG referenceCount_;
    CompositionManager* manager_;
    ITfContext* context_;
    EditAction action_;
    wchar_t character_;
    std::size_t candidateIndex_;
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
        // Shipped user-reviewed candidate mappings are kept separate from the
        // small core dictionary so duplicate cleanup and future updates are easy.
        for (const auto& extraFile : {
                 dataRoot_ / "data" / "merged_candidates.csv",
                 dataRoot_ / "data" / "loanwords.csv",
                 dataRoot_ / "data" / "imported_loanwords_alpha1083_pack2.csv",
                 dataRoot_ / "data" / "alpha08_candidates.csv"
             }) {
            std::string extraError;
            if (!dictionary.appendFromCsv(extraFile, &extraError, true)) {
                debugLog(std::string("Extra dictionary load failed: ") + extraError);
            }
        }

        // Personal suggestions live outside Program Files/build output so they
        // survive upgrades. A future Settings UI can write to this same file.
        const std::filesystem::path personalFile = userDictionaryPath();
        std::error_code createError;
        std::filesystem::create_directories(personalFile.parent_path(), createError);

        if (!std::filesystem::exists(personalFile, createError)) {
            std::ofstream personalTemplate(personalFile, std::ios::binary);
            if (personalTemplate.is_open()) {
                personalTemplate << "myanglish,burmese,frequency\n";
            }
        }

        std::string personalError;
        if (!dictionary.appendFromCsv(personalFile, &personalError, true)) {
            debugLog(std::string("User dictionary load failed: ") + personalError);
        }

        converter_ =
            std::make_unique<myanglish::MyanglishConverter>(
                std::move(dictionary),
                dataRoot_
            );

        loadUserHistory();
        loadLoanwordInputs();

        debugLog(
            std::string("Dictionary loaded: ") +
            dictionaryFile.string()
        );
        debugLog(std::string("User dictionary: ") + personalFile.string());
    } else {
        debugLog(
            std::string("Dictionary load failed: ") +
            errorMessage
        );
    }
}

CompositionManager::~CompositionManager() {
    releaseCompositionReference();
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

bool CompositionManager::lastTextRect(RECT& rect) const noexcept {
    if (!hasLastTextRect_) {
        return false;
    }
    rect = lastTextRect_;
    return true;
}

void CompositionManager::setClientId(
    TfClientId clientId
) noexcept {
    clientId_ = clientId;
}

void CompositionManager::setStackPrefixEnabled(bool enabled) noexcept {
    stackPrefixEnabled_ = enabled;
}

std::vector<std::wstring> CompositionManager::currentCandidateTexts(
    std::size_t limit
) const {
    std::vector<std::wstring> result;
    if (buffer_.empty() || limit == 0) {
        return result;
    }

    // R1.16: explicitly tagged loanwords always offer their original Roman
    // spelling as candidate #1. Myanmar transliterations follow at #2 onward.
    // Stack/kinzi compositions never offer a Roman candidate.
    if (isRawLoanwordCandidate(0)) {
        const std::wstring raw = utf8ToUtf16(buffer_);
        if (!raw.empty()) {
            result.push_back(raw);
        }
    }
    if (result.size() >= limit) {
        return result;
    }

    // Stable 0.10.8.3 pipeline first; adaptive ranking is only a post-process.
    if (converter_ != nullptr) {
        const std::size_t fetchLimit = std::max<std::size_t>(limit, 32);
        const auto candidates = converter_->getContinuousCandidates(buffer_, fetchLimit);

        struct RankedText {
            std::wstring text;
            std::size_t baseIndex = 0;
            int userScore = 0;
        };
        std::vector<RankedText> ranked;

        std::size_t index = 0;
        for (const auto& candidate : candidates) {
            if (candidate.burmese.empty() || candidate.burmese == buffer_) {
                ++index;
                continue;
            }

            std::wstring converted = utf8ToUtf16(candidate.burmese);
            if (stackPrefixEnabled_
                && !converted.empty()
                && converted.front() != static_cast<wchar_t>(0x1039)) {
                converted.insert(converted.begin(), static_cast<wchar_t>(0x1039));
            }

            if (!converted.empty()) {
                const bool duplicate = std::any_of(
                    ranked.begin(), ranked.end(),
                    [&](const RankedText& item) { return item.text == converted; }
                );
                if (!duplicate) {
                    ranked.push_back(RankedText{
                        converted,
                        index,
                        userChoiceScore(buffer_, converted)
                    });
                }
            }
            ++index;
        }

        std::stable_sort(
            ranked.begin(), ranked.end(),
            [](const RankedText& left, const RankedText& right) {
                if (left.userScore != right.userScore) {
                    return left.userScore > right.userScore;
                }
                return left.baseIndex < right.baseIndex;
            }
        );

        for (const auto& item : ranked) {
            const bool duplicate = std::any_of(
                result.begin(), result.end(),
                [&](const std::wstring& text) { return text == item.text; }
            );
            if (duplicate) {
                continue;
            }
            result.push_back(item.text);
            if (result.size() >= limit) {
                break;
            }
        }
    }

    return result;
}


void CompositionManager::loadLoanwordInputs() {
    loanwordInputs_.clear();

    // Every row in both reviewed loanword candidate files is a loanword.
    for (const auto& loanwordFile : {
             dataRoot_ / "data" / "loanwords.csv",
             dataRoot_ / "data" / "imported_loanwords_alpha1083_pack2.csv"
         }) {
        std::ifstream file(
            loanwordFile,
            std::ios::binary
        );

        std::string line;
        std::size_t lineNumber = 0;

        while (file.is_open() && std::getline(file, line)) {
            ++lineNumber;

            if (lineNumber == 1
                && line.rfind("\xEF\xBB\xBF", 0) == 0) {
                line.erase(0, 3);
            }

            if (line.empty()) {
                continue;
            }

            const auto comma = line.find(',');
            if (comma == std::string::npos) {
                continue;
            }

            std::string raw = line.substr(0, comma);

            for (char& ch : raw) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }

            if (lineNumber == 1 && raw == "myanglish") {
                continue;
            }

            if (!raw.empty()) {
                loanwordInputs_.insert(raw);
            }
        }
    }

    // Lexicon Pack 2 has explicit source metadata. Only source=loanword is added.
    {
        std::ifstream file(
            dataRoot_ / "data" / "imported_lexicon_alpha1083_pack2.csv",
            std::ios::binary
        );

        std::string line;
        std::size_t lineNumber = 0;

        while (file.is_open() && std::getline(file, line)) {
            ++lineNumber;

            if (lineNumber == 1
                && line.rfind("\xEF\xBB\xBF", 0) == 0) {
                line.erase(0, 3);
            }

            if (line.empty()) {
                continue;
            }

            std::vector<std::string> columns;
            std::size_t start = 0;

            while (start <= line.size()) {
                const auto comma = line.find(',', start);
                if (comma == std::string::npos) {
                    columns.push_back(line.substr(start));
                    break;
                }

                columns.push_back(
                    line.substr(start, comma - start)
                );

                start = comma + 1;
            }

            if (columns.size() < 4) {
                continue;
            }

            std::string raw = columns[0];
            std::string source = columns[3];

            for (char& ch : raw) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }

            for (char& ch : source) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }

            if (source == "loanword" && !raw.empty()) {
                loanwordInputs_.insert(raw);
            }
        }
    }

    debugLog(
        std::string("Loanword inputs loaded: ")
        + std::to_string(loanwordInputs_.size())
    );
}

bool CompositionManager::isCurrentLoanword() const noexcept {
    if (buffer_.empty()) {
        return false;
    }

    std::string raw = buffer_;

    for (char& ch : raw) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }

    return loanwordInputs_.find(raw)
        != loanwordInputs_.end();
}

bool CompositionManager::isRawLoanwordCandidate(
    std::size_t candidateIndex
) const noexcept {
    return candidateIndex == 0
        && isCurrentLoanword()
        && !stackPrefixEnabled_
        && stackJoinPrefix_.empty()
        && !kinziPending_;
}

void CompositionManager::loadUserHistory() {
    userHistory_.clear();

    std::ifstream file(userHistoryPath(), std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.rfind("myanglish,", 0) == 0) {
            continue;
        }

        std::stringstream stream(line);
        std::string raw;
        std::string candidateUtf8;
        std::string countText;
        std::string lastText;

        if (!std::getline(stream, raw, ',')
            || !std::getline(stream, candidateUtf8, ',')
            || !std::getline(stream, countText, ',')
            || !std::getline(stream, lastText)) {
            continue;
        }

        try {
            const std::wstring candidate = utf8ToUtf16(candidateUtf8);
            if (raw.empty() || candidate.empty()) {
                continue;
            }

            userHistory_[raw][candidate] = UserChoiceStats{
                std::stoi(countText),
                std::stoll(lastText)
            };
        } catch (...) {
        }
    }
}

int CompositionManager::userChoiceScore(
    const std::string& raw,
    const std::wstring& candidate
) const {
    const auto rawIt = userHistory_.find(raw);
    if (rawIt == userHistory_.end()) {
        return 0;
    }

    const auto choiceIt = rawIt->second.find(candidate);
    if (choiceIt == rawIt->second.end()) {
        return 0;
    }

    const auto& stats = choiceIt->second;
    int recencyBonus = 0;
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const std::int64_t age = now > stats.lastUsed ? now - stats.lastUsed : 0;

    if (age <= 86400) {
        recencyBonus = 1200;
    } else if (age <= 7 * 86400) {
        recencyBonus = 600;
    } else if (age <= 30 * 86400) {
        recencyBonus = 200;
    }

    return stats.count * 2500 + recencyBonus;
}

void CompositionManager::recordUserSelection(
    const std::string& raw,
    const std::wstring& candidate
) {
    if (raw.empty() || candidate.empty()) {
        return;
    }

    auto& stats = userHistory_[raw][candidate];
    ++stats.count;
    stats.lastUsed = static_cast<std::int64_t>(std::time(nullptr));

    try {
        const auto path = userHistoryPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return;
        }

        file << "myanglish,burmese,count,last_used\n";
        for (const auto& rawPair : userHistory_) {
            for (const auto& choicePair : rawPair.second) {
                file << rawPair.first << ","
                     << utf16ToUtf8(choicePair.first) << ","
                     << choicePair.second.count << ","
                     << choicePair.second.lastUsed << "\n";
            }
        }
    } catch (...) {
    }
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

HRESULT CompositionManager::previewCandidate(
    ITfContext* context,
    std::size_t candidateIndex
) {
    return requestEdit(
        context,
        EditAction::PreviewCandidate,
        0,
        candidateIndex
    );
}

HRESULT CompositionManager::restoreOriginalPreview(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::RestoreOriginalPreview
    );
}

HRESULT CompositionManager::toggleRawWord(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::ToggleRawWord
    );
}

HRESULT CompositionManager::lockCandidate(
    ITfContext* context,
    std::size_t candidateIndex
) {
    return requestEdit(
        context,
        EditAction::LockCandidate,
        0,
        candidateIndex
    );
}

HRESULT CompositionManager::commitCandidate(
    ITfContext* context,
    std::size_t candidateIndex
) {
    return requestEdit(
        context,
        EditAction::CommitCandidate,
        0,
        candidateIndex
    );
}

HRESULT CompositionManager::commitKinziShortcut(
    ITfContext* context,
    std::size_t candidateIndex
) {
    return requestEdit(
        context,
        EditAction::CommitKinziShortcut,
        0,
        candidateIndex
    );
}

HRESULT CompositionManager::beginKinziPending(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::BeginKinziPending
    );
}

HRESULT CompositionManager::commitStackShortcut(
    ITfContext* context,
    std::size_t candidateIndex
) {
    return requestEdit(
        context,
        EditAction::CommitStackShortcut,
        0,
        candidateIndex
    );
}

HRESULT CompositionManager::beginStackJoin(
    ITfContext* context,
    std::size_t previousCandidateIndex,
    wchar_t character
) {
    return requestEdit(
        context,
        EditAction::BeginStackJoin,
        character,
        previousCandidateIndex
    );
}

HRESULT CompositionManager::commitVisiblePreview(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::CommitVisiblePreview
    );
}

HRESULT CompositionManager::commitVisiblePreviewAndStartNext(
    ITfContext* context,
    wchar_t character
) {
    return requestEdit(
        context,
        EditAction::CommitVisiblePreviewAndStartNext,
        character,
        0
    );
}

HRESULT CompositionManager::commitVisiblePreviewAndInsertLiteral(
    ITfContext* context,
    wchar_t literal
) {
    return requestEdit(
        context,
        EditAction::CommitVisiblePreviewAndInsertLiteral,
        literal,
        0
    );
}

HRESULT CompositionManager::commitCandidateAndStartNext(
    ITfContext* context,
    std::size_t candidateIndex,
    wchar_t character
) {
    return requestEdit(
        context,
        EditAction::CommitCandidateAndStartNext,
        character,
        candidateIndex
    );
}

HRESULT CompositionManager::commitCandidateAndInsertLiteral(
    ITfContext* context,
    std::size_t candidateIndex,
    wchar_t literal
) {
    return requestEdit(context, EditAction::CommitCandidateAndInsertLiteral, literal, candidateIndex);
}

HRESULT CompositionManager::commitBestCandidateAndInsertLiteral(
    ITfContext* context,
    wchar_t literal
) {
    return requestEdit(context, EditAction::CommitBestCandidateAndInsertLiteral, literal, 0);
}

HRESULT CompositionManager::insertLiteral(
    ITfContext* context,
    wchar_t literal
) {
    return requestEdit(context, EditAction::InsertLiteral, literal, 0);
}

HRESULT CompositionManager::commitOriginal(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::CommitOriginal
    );
}

HRESULT CompositionManager::commitOriginalAndInsertLiteral(
    ITfContext* context,
    wchar_t literal
) {
    return requestEdit(
        context,
        EditAction::CommitOriginalAndInsertLiteral,
        literal,
        0
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

HRESULT CompositionManager::finalizeIfSelectionMoved(
    ITfContext* context
) {
    return requestEdit(
        context,
        EditAction::FinalizeIfSelectionMoved
    );
}

void CompositionManager::clearWithoutContext() noexcept {
    buffer_.clear();
    lastPreviewCandidate_.clear();
    rawPreview_ = false;
    stackPrefixEnabled_ = false;
    stackJoinPrefix_.clear();
    kinziPending_ = false;
    hasLastTextRect_ = false;
    releaseCompositionReference();
}

void CompositionManager::releaseCompositionReference() noexcept {
    if (composition_ != nullptr) {
        composition_->Release();
        composition_ = nullptr;
    }
    if (compositionContext_ != nullptr) {
        compositionContext_->Release();
        compositionContext_ = nullptr;
    }
}




HRESULT CompositionManager::applyCandidateIndicator(
    TfEditCookie ec,
    ITfContext* context,
    bool visible
) {
    if(!context || !composition_) return S_FALSE;

    ITfRange* range=nullptr;
    HRESULT hr=composition_->GetRange(&range);
    if(FAILED(hr)||!range) return FAILED(hr)?hr:E_FAIL;

    ITfProperty* property=nullptr;
    hr=context->GetProperty(GUID_PROP_ATTRIBUTE,&property);
    if(FAILED(hr)||!property) {
        range->Release();
        return FAILED(hr)?hr:E_FAIL;
    }

    const TfGuidAtom atom =
        candidateIndicatorAtom(visible);

    if(atom==TF_INVALID_GUIDATOM) {
        property->Release();
        range->Release();
        return S_FALSE;
    }

    VARIANT v;
    VariantInit(&v);
    v.vt=VT_I4;
    v.lVal=(LONG)atom;

    hr=property->SetValue(ec,range,&v);
    VariantClear(&v);

    property->Release(); range->Release();
    return hr;
}

bool CompositionManager::needsLeadingRawBoundarySpace(
    TfEditCookie editCookie
) const {
    if (composition_ == nullptr) {
        return false;
    }

    ITfRange* compositionRange = nullptr;
    const HRESULT rangeResult = composition_->GetRange(&compositionRange);
    if (FAILED(rangeResult) || compositionRange == nullptr) {
        if (compositionRange != nullptr) {
            compositionRange->Release();
        }
        return false;
    }

    ITfRange* previousRange = nullptr;
    HRESULT previousResult = compositionRange->Clone(&previousRange);
    compositionRange->Release();

    if (SUCCEEDED(previousResult) && previousRange != nullptr) {
        previousResult = previousRange->Collapse(
            editCookie,
            TF_ANCHOR_START
        );
    }

    LONG shifted = 0;
    if (SUCCEEDED(previousResult) && previousRange != nullptr) {
        previousResult = previousRange->ShiftStart(
            editCookie,
            -1,
            &shifted,
            nullptr
        );
    }

    bool needsSpace = false;
    if (SUCCEEDED(previousResult)
        && previousRange != nullptr
        && shifted == -1) {
        wchar_t previousText[2]{};
        ULONG previousLength = 0;
        const HRESULT readResult = previousRange->GetText(
            editCookie,
            0,
            previousText,
            1,
            &previousLength
        );

        if (SUCCEEDED(readResult) && previousLength == 1) {
            const wchar_t previous = previousText[0];
            const bool previousIsWhitespace =
                previous == L' '
                || previous == L'\t'
                || previous == L'\r'
                || previous == L'\n';
            needsSpace = !previousIsWhitespace;
        }
    }

    if (previousRange != nullptr) {
        previousRange->Release();
    }
    return needsSpace;
}

HRESULT CompositionManager::requestEdit(
    ITfContext* context,
    EditAction action,
    wchar_t character,
    std::size_t candidateIndex
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
            character,
            candidateIndex
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
    wchar_t character,
    std::size_t candidateIndex
) {
    if (context == nullptr) {
        return E_POINTER;
    }

    switch (action) {
    case EditAction::InsertCharacter: {
        const bool asciiLetter =
            (character >= L'A' && character <= L'Z')
            || (character >= L'a' && character <= L'z');
        if (!asciiLetter && character != L'*') {
            return E_INVALIDARG;
        }

        char asciiCharacter = static_cast<char>(character);
        if (asciiCharacter >= 'A' && asciiCharacter <= 'Z') {
            asciiCharacter = static_cast<char>(asciiCharacter - 'A' + 'a');
        }

        // alpha-0.8.8 Romaji-style composition rule:
        // While letters are being typed, keep the raw Myanglish spelling visible.
        // Conversion begins only when Space is pressed. This mirrors Japanese
        // Romaji IME composition and avoids guessing a word boundary while typing.
        std::string nextBuffer = buffer_;
        nextBuffer.push_back(asciiCharacter);

        const std::wstring rawSegment = utf8ToUtf16(nextBuffer);
        if (rawSegment.empty() && !nextBuffer.empty()) {
            return E_FAIL;
        }

        const std::wstring visibleText =
            kinziPending_
                ? rawSegment
                : (stackJoinPrefix_ + rawSegment);

        const HRESULT result = updateCompositionText(
            editCookie,
            context,
            visibleText
        );
        if (SUCCEEDED(result)) {
            buffer_ = std::move(nextBuffer);
            rawPreview_ = true;
            const bool candidateAvailable = !currentCandidateTexts(1).empty();
            const HRESULT indicatorHr =
                applyCandidateIndicator(editCookie, context, candidateAvailable);
            if (FAILED(indicatorHr)) {
                debugLogHr("candidate underline after typing", indicatorHr);
            }
            debugLog("Raw Myanglish composition updated; candidate underline refreshed");
        }
        return result;
    }

    case EditAction::DeleteBackspace: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        buffer_.pop_back();
        rawPreview_ = false;

        if (buffer_.empty()) {
            const HRESULT clearResult = updateCompositionText(
                editCookie,
                context,
                L""
            );
            if (FAILED(clearResult)) {
                return clearResult;
            }
            const HRESULT endResult = endComposition(editCookie);
            if (SUCCEEDED(endResult)) {
                hasLastTextRect_ = false;
                kinziPending_ = false;
                stackPrefixEnabled_ = false;
                stackJoinPrefix_.clear();
                lastPreviewCandidate_.clear();
            }
            return endResult;
        }

        rawPreview_ = true;
        const std::wstring rawSegment = utf8ToUtf16(buffer_);
        if (rawSegment.empty()) {
            return E_FAIL;
        }
        const HRESULT updateResult = updateCompositionText(
            editCookie,
            context,
            stackJoinPrefix_ + rawSegment
        );
        if (SUCCEEDED(updateResult)) {
            const bool candidateAvailable = !currentCandidateTexts(1).empty();
            const HRESULT indicatorHr =
                applyCandidateIndicator(editCookie, context, candidateAvailable);
            if (FAILED(indicatorHr)) {
                debugLogHr("candidate underline after raw Backspace", indicatorHr);
            }
        }
        return updateResult;
    }

    case EditAction::PreviewCandidate: {
        // Keep R1.3 Myanmar-preview styling unchanged:
        // remove the raw candidate/no-candidate indicator before conversion preview.
        if (composition_ != nullptr) {
            ITfRange* indicatorRange=nullptr;
            if (SUCCEEDED(composition_->GetRange(&indicatorRange))
                && indicatorRange!=nullptr) {

                ITfProperty* indicatorProperty=nullptr;
                if (SUCCEEDED(context->GetProperty(
                        GUID_PROP_ATTRIBUTE,
                        &indicatorProperty
                    ))
                    && indicatorProperty!=nullptr) {

                    (void)indicatorProperty->Clear(
                        editCookie,
                        indicatorRange
                    );
                    indicatorProperty->Release();
                }
                indicatorRange->Release();
            }
        }

        if (buffer_.empty()) {
            return S_FALSE;
        }

        const auto candidates = currentCandidateTexts(candidateIndex + 1);
        if (candidateIndex >= candidates.size() || candidates[candidateIndex].empty()) {
            return E_INVALIDARG;
        }

        std::wstring preview = candidates[candidateIndex];
        if (isRawLoanwordCandidate(candidateIndex)) {
            if (needsLeadingRawBoundarySpace(editCookie)) {
                preview.insert(preview.begin(), L' ');
            }
            preview.push_back(L' ');
        }

        rawPreview_ = false;
        const HRESULT previewResult = updateCompositionText(
            editCookie,
            context,
            kinziPending_
                ? preview
                : (stackJoinPrefix_ + preview)
        );
        if (SUCCEEDED(previewResult)) {
            lastPreviewCandidate_ = candidates[candidateIndex];
        }
        return previewResult;
    }

    case EditAction::RestoreOriginalPreview: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        rawPreview_ = true;
        lastPreviewCandidate_.clear();
        const std::wstring originalSegment = utf8ToUtf16(buffer_);
        if (originalSegment.empty()) {
            return E_FAIL;
        }

        const HRESULT updateResult = updateCompositionText(
            editCookie,
            context,
            kinziPending_
                ? originalSegment
                : (stackJoinPrefix_ + originalSegment)
        );
        if (SUCCEEDED(updateResult)) {
            const bool candidateAvailable = !currentCandidateTexts(1).empty();
            (void)applyCandidateIndicator(editCookie, context, candidateAvailable);
        }
        return updateResult;
    }

    case EditAction::ToggleRawWord: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        if (rawPreview_) {
            rawPreview_ = false;
            const std::wstring converted = makeBestSegmentText(buffer_);
            if (converted.empty()) {
                return E_FAIL;
            }
            debugLog("Current word restored from raw Roman to Burmese candidate");
            return updateCompositionText(
                editCookie,
                context,
                kinziPending_
                    ? converted
                    : (stackJoinPrefix_ + converted)
            );
        }

        const std::wstring raw = utf8ToUtf16(buffer_);
        if (raw.empty()) {
            return E_FAIL;
        }
        rawPreview_ = true;
        debugLog("Current word toggled to raw Roman text");
        return updateCompositionText(
            editCookie,
            context,
            kinziPending_
                ? raw
                : (stackJoinPrefix_ + raw)
        );
    }

    case EditAction::LockCandidate: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        const auto candidates = currentCandidateTexts(candidateIndex + 1);
        if (candidateIndex >= candidates.size() || candidates[candidateIndex].empty()) {
            return E_INVALIDARG;
        }

        const std::string finishedRaw = buffer_;
        const std::wstring selected = candidates[candidateIndex];

        HRESULT result = updateCompositionText(
            editCookie,
            context,
            selected
        );
        if (FAILED(result)) {
            return result;
        }

        result = endComposition(editCookie);
        if (SUCCEEDED(result)) {
            (void)finishedRaw;
            buffer_.clear();
            rawPreview_ = false;
            debugLog("Selected candidate auto-committed before next word");
        }
        return result;
    }

    case EditAction::BeginKinziPending: {
        // Alpha 0.10.7:
        // "\" means "insert kinzi here and attach the NEXT syllable after it".
        //
        // Example:
        //   အလ + \      -> အလင်္
        //   then kar    -> အလင်္kar (raw)
        //   Space       -> အလင်္ကာ
        //
        // We insert kinzi at the current caret, then keep it as a prefix inside
        // the next composition instead of moving it into the previous syllable.
        TF_SELECTION selection{};
        ULONG fetched = 0;
        HRESULT result = context->GetSelection(
            editCookie,
            TF_DEFAULT_SELECTION,
            1,
            &selection,
            &fetched
        );

        if (FAILED(result) || fetched != 1 || selection.range == nullptr) {
            return FAILED(result) ? result : E_FAIL;
        }

        const wchar_t kinzi[] = {
            static_cast<wchar_t>(0x1004), // င
            static_cast<wchar_t>(0x103A), // ်
            static_cast<wchar_t>(0x1039)  // ္
        };

        result = selection.range->SetText(
            editCookie,
            0,
            kinzi,
            static_cast<LONG>(std::size(kinzi))
        );

        if (SUCCEEDED(result)) {
            result = selection.range->Collapse(editCookie, TF_ANCHOR_END);
        }

        if (SUCCEEDED(result)) {
            TF_SELECTION after{};
            after.range = selection.range;
            after.style.ase = TF_AE_NONE;
            after.style.fInterimChar = FALSE;
            result = context->SetSelection(editCookie, 1, &after);
        }

        selection.range->Release();

        if (SUCCEEDED(result)) {
            kinziPending_ = true;
            stackJoinPrefix_ = L"င်္";
            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            hasLastTextRect_ = false;
            debugLog("Kinzi pending mode armed by backslash");
        }

        return result;
    }

    case EditAction::CommitKinziShortcut: {
        if (buffer_.empty() || composition_ == nullptr) {
            return S_FALSE;
        }

        // R1.15: use the exact candidate currently visible to the user.
        std::wstring candidate;

        if (!lastPreviewCandidate_.empty()) {
            candidate = lastPreviewCandidate_;
        } else {
            const auto candidates =
                currentCandidateTexts(
                    candidateIndex + 1
                );

            if (
                candidateIndex >= candidates.size()
                || candidates[candidateIndex].empty()
            ) {
                return S_FALSE;
            }

            candidate = candidates[candidateIndex];
        }

        while (
            !candidate.empty()
            && candidate.front()
                == static_cast<wchar_t>(0x1039)
        ) {
            candidate.erase(candidate.begin());
        }

        if (candidate.empty()) {
            return S_FALSE;
        }

        const wchar_t first =
            candidate.front();

        const bool candidateStartsConsonant =
            first >= static_cast<wchar_t>(0x1000)
            && first <= static_cast<wchar_t>(0x1021);

        if (!candidateStartsConsonant) {
            return S_FALSE;
        }

        ITfRange* compositionRange = nullptr;

        HRESULT result =
            composition_->GetRange(
                &compositionRange
            );

        if (
            FAILED(result)
            || compositionRange == nullptr
        ) {
            if (compositionRange != nullptr) {
                compositionRange->Release();
            }

            return FAILED(result)
                ? result
                : E_FAIL;
        }

        // Read up to 3 committed code points before current raw composition.
        // Supported:
        //   င + ်       -> င + ် + ္
        //   င + ် + း  -> င + ် + ္   (း auto deleted)
        ITfRange* previousRange = nullptr;

        result =
            compositionRange->Clone(
                &previousRange
            );

        if (
            SUCCEEDED(result)
            && previousRange != nullptr
        ) {
            result =
                previousRange->Collapse(
                    editCookie,
                    TF_ANCHOR_START
                );
        }

        LONG shifted = 0;

        if (
            SUCCEEDED(result)
            && previousRange != nullptr
        ) {
            result =
                previousRange->ShiftStart(
                    editCookie,
                    -3,
                    &shifted,
                    nullptr
                );
        }

        if (
            FAILED(result)
            || previousRange == nullptr
        ) {
            if (previousRange != nullptr) {
                previousRange->Release();
            }

            compositionRange->Release();
            return FAILED(result)
                ? result
                : S_FALSE;
        }

        wchar_t tailText[8]{};
        ULONG tailLength = 0;

        result =
            previousRange->GetText(
                editCookie,
                0,
                tailText,
                static_cast<ULONG>(
                    std::size(tailText)
                ),
                &tailLength
            );

        if (
            FAILED(result)
            || tailLength < 2
        ) {
            previousRange->Release();
            compositionRange->Release();

            return FAILED(result)
                ? result
                : S_FALSE;
        }

        std::wstring tail(
            tailText,
            tailLength
        );

        LONG replaceCount = 0;

        // Pattern: င်း
        if (
            tail.size() >= 3
            && tail[tail.size() - 3]
                == static_cast<wchar_t>(0x1004)
            && tail[tail.size() - 2]
                == static_cast<wchar_t>(0x103A)
            && tail[tail.size() - 1]
                == static_cast<wchar_t>(0x1038)
        ) {
            replaceCount = 3;
        }
        // Pattern: င်
        else if (
            tail.size() >= 2
            && tail[tail.size() - 2]
                == static_cast<wchar_t>(0x1004)
            && tail[tail.size() - 1]
                == static_cast<wchar_t>(0x103A)
        ) {
            replaceCount = 2;
        }

        previousRange->Release();
        previousRange = nullptr;

        if (replaceCount == 0) {
            compositionRange->Release();

            // Critical R1.8 reset:
            // rejected stack attempts never leave a pending kinzi state.
            kinziPending_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();

            return S_FALSE;
        }

        ITfRange* replaceRange = nullptr;

        result =
            compositionRange->Clone(
                &replaceRange
            );

        compositionRange->Release();
        compositionRange = nullptr;

        LONG replaceShift = 0;

        if (
            SUCCEEDED(result)
            && replaceRange != nullptr
        ) {
            result =
                replaceRange->ShiftStart(
                    editCookie,
                    -replaceCount,
                    &replaceShift,
                    nullptr
                );
        }

        if (
            FAILED(result)
            || replaceRange == nullptr
            || replaceShift != -replaceCount
        ) {
            if (replaceRange != nullptr) {
                replaceRange->Release();
            }

            kinziPending_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();

            return FAILED(result)
                ? result
                : S_FALSE;
        }

        std::wstring replacement;

        // IMPORTANT:
        // င် is preserved exactly as င + ်, then VIRAMA is appended.
        // So stack form is င + ် + ္, not င + ္.
        replacement.push_back(
            static_cast<wchar_t>(0x1004)
        ); // င

        replacement.push_back(
            static_cast<wchar_t>(0x103A)
        ); // ်

        replacement.push_back(
            static_cast<wchar_t>(0x1039)
        ); // ္

        replacement += candidate;

        result =
            endComposition(
                editCookie
            );

        if (FAILED(result)) {
            replaceRange->Release();

            kinziPending_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();

            return result;
        }

        result =
            replaceRange->SetText(
                editCookie,
                0,
                replacement.c_str(),
                static_cast<LONG>(
                    replacement.size()
                )
            );

        if (SUCCEEDED(result)) {
            result =
                replaceRange->Collapse(
                    editCookie,
                    TF_ANCHOR_END
                );
        }

        if (SUCCEEDED(result)) {
            TF_SELECTION selection{};
            selection.range = replaceRange;
            selection.style.ase = TF_AE_NONE;
            selection.style.fInterimChar = FALSE;

            result =
                context->SetSelection(
                    editCookie,
                    1,
                    &selection
                );
        }

        replaceRange->Release();

        // Absolute one-shot reset.
        // No kinzi prefix is allowed to leak into the next word.
        buffer_.clear();
        lastPreviewCandidate_.clear();
        rawPreview_ = false;
        kinziPending_ = false;
        stackPrefixEnabled_ = false;
        stackJoinPrefix_.clear();
        hasLastTextRect_ = false;

        if (SUCCEEDED(result)) {
            debugLog(
                "R1.8 Ctrl+Enter kinzi stack committed; "
                "all kinzi/stack state cleared"
            );
        }

        return result;
    }

    case EditAction::CommitStackShortcut: {
        if (buffer_.empty() || composition_ == nullptr) {
            return S_FALSE;
        }

        // R1.15 SYSTEM STACK:
        // Stack the exact candidate currently previewed/visible.
        //
        // Example:
        //   thit -> သစ်
        //   sar -> စား
        //   change candidate to စာ
        //   Ctrl+Enter -> သစ္စာ
        //
        // If there is no visible preview yet, use the current candidate index.
        std::wstring candidate;

        if (!lastPreviewCandidate_.empty()) {
            candidate = lastPreviewCandidate_;
        } else {
            const auto candidates =
                currentCandidateTexts(
                    candidateIndex + 1
                );

            if (
                candidateIndex >= candidates.size()
                || candidates[candidateIndex].empty()
            ) {
                return S_FALSE;
            }

            candidate = candidates[candidateIndex];
        }

        // The special R1.8a င်/င်္ stack path is still handled BEFORE this
        // action by CommitKinziShortcut.

        // Ctrl+Enter itself owns the virama. Never allow a candidate to
        // contribute an extra leading virama.
        while (
            !candidate.empty()
            && candidate.front()
                == static_cast<wchar_t>(0x1039)
        ) {
            candidate.erase(candidate.begin());
        }

        if (candidate.empty()) {
            return S_FALSE;
        }

        const wchar_t candidateFirst =
            candidate.front();

        const bool candidateStartsConsonant =
            candidateFirst
                >= static_cast<wchar_t>(0x1000)
            && candidateFirst
                <= static_cast<wchar_t>(0x1021);

        if (!candidateStartsConsonant) {
            return S_FALSE;
        }

        ITfRange* compositionRange = nullptr;

        HRESULT result =
            composition_->GetRange(
                &compositionRange
            );

        if (
            FAILED(result)
            || compositionRange == nullptr
        ) {
            if (compositionRange != nullptr) {
                compositionRange->Release();
            }

            return FAILED(result)
                ? result
                : E_FAIL;
        }

        // Read exactly the two committed code points immediately before the
        // current raw composition.
        //
        // Example:
        //   တက်  = တ + က + ်
        //                    ↑ last two = က + ်
        //
        // Then:
        //   က + ်  -> က + ္
        //   current က appended
        //   => က္က
        // Whole text => တက္က
        ITfRange* previousRange = nullptr;

        result =
            compositionRange->Clone(
                &previousRange
            );

        if (
            SUCCEEDED(result)
            && previousRange != nullptr
        ) {
            result =
                previousRange->Collapse(
                    editCookie,
                    TF_ANCHOR_START
                );
        }

        LONG shifted = 0;

        if (
            SUCCEEDED(result)
            && previousRange != nullptr
        ) {
            result =
                previousRange->ShiftStart(
                    editCookie,
                    -3,
                    &shifted,
                    nullptr
                );
        }

        if (
            FAILED(result)
            || previousRange == nullptr
            || (shifted != -2 && shifted != -3)
        ) {
            if (previousRange != nullptr) {
                previousRange->Release();
            }

            compositionRange->Release();

            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;

            return FAILED(result)
                ? result
                : S_FALSE;
        }

        wchar_t previousText[4]{};
        ULONG previousLength = 0;

        result =
            previousRange->GetText(
                editCookie,
                0,
                previousText,
                static_cast<ULONG>(
                    std::size(previousText)
                ),
                &previousLength
            );

        previousRange->Release();
        previousRange = nullptr;

        if (
            FAILED(result)
            || previousLength < 2
        ) {
            compositionRange->Release();

            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;

            return FAILED(result)
                ? result
                : S_FALSE;
        }

        // R1.17 tail decoder:
        //
        // Normal:
        //   C + ASAT
        //   က် + က -> က္က
        //
        // With trailing visarga:
        //   C + ASAT + VISARGA
        //   န်း + တ -> န္တ
        //
        // Therefore:
        //   မန်း + တ + Ctrl+Enter -> မန္တ
        //
        // Only the trailing VISARGA (း) belonging to this stack boundary is
        // dropped. Other text and all other IME behavior stay unchanged.
        const std::wstring previousTail(
            previousText,
            previousLength
        );

        wchar_t previousConsonant = 0;
        LONG replaceCount = 0;

        if (
            previousTail.size() >= 3
            && previousTail[previousTail.size() - 1]
                == static_cast<wchar_t>(0x1038) // း
            && previousTail[previousTail.size() - 2]
                == static_cast<wchar_t>(0x103A) // ်
        ) {
            const wchar_t possibleConsonant =
                previousTail[previousTail.size() - 3];

            const bool isMyanmarConsonant =
                possibleConsonant
                    >= static_cast<wchar_t>(0x1000)
                && possibleConsonant
                    <= static_cast<wchar_t>(0x1021);

            if (isMyanmarConsonant) {
                previousConsonant = possibleConsonant;
                replaceCount = 3;
            }
        }

        if (
            replaceCount == 0
            && previousTail.size() >= 2
            && previousTail[previousTail.size() - 1]
                == static_cast<wchar_t>(0x103A) // ်
        ) {
            const wchar_t possibleConsonant =
                previousTail[previousTail.size() - 2];

            const bool isMyanmarConsonant =
                possibleConsonant
                    >= static_cast<wchar_t>(0x1000)
                && possibleConsonant
                    <= static_cast<wchar_t>(0x1021);

            if (isMyanmarConsonant) {
                previousConsonant = possibleConsonant;
                replaceCount = 2;
            }
        }

        // င + ် / င + ် + း stays owned by the existing special
        // CommitKinziShortcut path. Do not change that stable behavior.
        const bool isSpecialNgaAsat =
            previousConsonant
                == static_cast<wchar_t>(0x1004)
            && replaceCount != 0;

        if (
            replaceCount == 0
            || isSpecialNgaAsat
        ) {
            compositionRange->Release();

            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;

            return S_FALSE;
        }

        // Cover the previous C+ASAT(+optional VISARGA) and the current raw
        // composition with one replacement range.
        ITfRange* replaceRange = nullptr;

        result =
            compositionRange->Clone(
                &replaceRange
            );

        compositionRange->Release();
        compositionRange = nullptr;

        LONG replaceShift = 0;

        if (
            SUCCEEDED(result)
            && replaceRange != nullptr
        ) {
            result =
                replaceRange->ShiftStart(
                    editCookie,
                    -replaceCount,
                    &replaceShift,
                    nullptr
                );
        }

        if (
            FAILED(result)
            || replaceRange == nullptr
            || replaceShift != -replaceCount
        ) {
            if (replaceRange != nullptr) {
                replaceRange->Release();
            }

            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;

            return FAILED(result)
                ? result
                : S_FALSE;
        }

        std::wstring replacement;

        replacement.push_back(
            previousConsonant
        );

        replacement.push_back(
            static_cast<wchar_t>(0x1039)
        ); // ္

        replacement += candidate;

        // Finish the raw Roman composition before replacing the combined range.
        result =
            endComposition(
                editCookie
            );

        if (FAILED(result)) {
            replaceRange->Release();

            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;

            return result;
        }

        result =
            replaceRange->SetText(
                editCookie,
                0,
                replacement.c_str(),
                static_cast<LONG>(
                    replacement.size()
                )
            );

        if (SUCCEEDED(result)) {
            result =
                replaceRange->Collapse(
                    editCookie,
                    TF_ANCHOR_END
                );
        }

        if (SUCCEEDED(result)) {
            TF_SELECTION selection{};
            selection.range = replaceRange;
            selection.style.ase = TF_AE_NONE;
            selection.style.fInterimChar = FALSE;

            result =
                context->SetSelection(
                    editCookie,
                    1,
                    &selection
                );
        }

        replaceRange->Release();

        // One-shot cleanup. No normal-stack state is allowed to leak into the
        // next word.
        buffer_.clear();
        lastPreviewCandidate_.clear();
        rawPreview_ = false;
        stackPrefixEnabled_ = false;
        stackJoinPrefix_.clear();
        kinziPending_ = false;
        hasLastTextRect_ = false;

        if (SUCCEEDED(result)) {
            debugLog(
                "R1.15 selected-candidate normal Ctrl+Enter stack committed"
            );
        }

        return result;
    }

    case EditAction::BeginStackJoin: {
        if (buffer_.empty() || composition_ == nullptr) {
            return S_FALSE;
        }

        if (character < L'A'
            || (character > L'Z' && character < L'a')
            || character > L'z') {
            return E_INVALIDARG;
        }

        // IMPORTANT:
        // Do NOT end the previous composition and then start a new composition
        // whose first code point is U+1039 VIRAMA. A leading combining virama
        // can attach across the TSF composition boundary, which caused:
        //
        //   preview : အင်္ဂါ
        //   Enter   : အင်္္ဂါ
        //   next key: အင်္္ဂsarါ
        //
        // Keep the complete previous Burmese candidate in THIS SAME
        // composition, then continue the stacked segment inside the same range.
        const auto previousCandidates =
            currentCandidateTexts(candidateIndex + 1);

        if (candidateIndex >= previousCandidates.size()
            || previousCandidates[candidateIndex].empty()) {
            return E_INVALIDARG;
        }

        // If this is a second/third stacked segment, stackJoinPrefix_ already
        // contains all older segments. currentCandidateTexts() contains only the
        // current segment, including one leading U+1039 when stack mode is active.
        std::wstring completePrevious =
            stackJoinPrefix_ + previousCandidates[candidateIndex];

        if (completePrevious.empty()) {
            return E_FAIL;
        }

        stackJoinPrefix_ = std::move(completePrevious);

        buffer_.clear();
        rawPreview_ = true;
        stackPrefixEnabled_ = true;

        char asciiCharacter = static_cast<char>(character);
        if (asciiCharacter >= 'A' && asciiCharacter <= 'Z') {
            asciiCharacter =
                static_cast<char>(asciiCharacter - 'A' + 'a');
        }
        buffer_.push_back(asciiCharacter);

        const std::wstring rawSegment = utf8ToUtf16(buffer_);
        if (rawSegment.empty()) {
            return E_FAIL;
        }

        const HRESULT result = updateCompositionText(
            editCookie,
            context,
            stackJoinPrefix_ + rawSegment
        );

        if (SUCCEEDED(result)) {
            debugLog(
                "Stack join started inside existing composition; "
                "no leading-virama composition boundary"
            );
        }
        return result;
    }

    case EditAction::CommitVisiblePreview:
    case EditAction::CommitVisiblePreviewAndStartNext:
    case EditAction::CommitVisiblePreviewAndInsertLiteral: {
        if (buffer_.empty() || composition_ == nullptr) {
            return S_FALSE;
        }

        const std::string learningRaw = buffer_;
        const std::wstring learningCandidate = lastPreviewCandidate_;

        if (action == EditAction::CommitVisiblePreviewAndStartNext) {
            if (character < L'A'
                || (character > L'Z' && character < L'a')
                || character > L'z') {
                return E_INVALIDARG;
            }
        }

        // The selected candidate is ALREADY visible in the composition.
        // Do not call SetText() with the candidate again here.
        //
        // A leading U+1039 virama can combine with the preceding committed
        // syllable. Some TSF text stores can then adjust the composition range
        // around that combining mark. Rewriting the same preview at commit time
        // can therefore produce U+1039 twice:
        //
        //     preview:  အင်္ဂါ
        //     commit:   အင်္္ဂါ
        //
        // Accept the exact visible composition instead, and preserve its END
        // range so the host caret is explicitly restored after EndComposition.
        ITfRange* acceptedRange = nullptr;
        HRESULT result = composition_->GetRange(&acceptedRange);
        if (FAILED(result) || acceptedRange == nullptr) {
            if (acceptedRange != nullptr) acceptedRange->Release();
            return FAILED(result) ? result : E_FAIL;
        }

        ITfRange* acceptedEnd = nullptr;
        result = acceptedRange->Clone(&acceptedEnd);
        acceptedRange->Release();
        acceptedRange = nullptr;

        if (SUCCEEDED(result) && acceptedEnd != nullptr) {
            result = acceptedEnd->Collapse(editCookie, TF_ANCHOR_END);
        }
        if (FAILED(result) || acceptedEnd == nullptr) {
            if (acceptedEnd != nullptr) acceptedEnd->Release();
            return FAILED(result) ? result : E_FAIL;
        }

        result = endComposition(editCookie);
        if (FAILED(result)) {
            acceptedEnd->Release();
            return result;
        }

        recordUserSelection(learningRaw, learningCandidate);
        lastPreviewCandidate_.clear();

        // EndComposition may cause some host editors to move their own caret.
        // Put it back at the accepted preview's exact end before doing anything
        // else. This is host-neutral TSF selection handling.
        TF_SELECTION selection{};
        selection.range = acceptedEnd;
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        result = context->SetSelection(editCookie, 1, &selection);
        if (FAILED(result)) {
            debugLogHr("SetSelection after visible-preview commit", result);
            acceptedEnd->Release();
            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            hasLastTextRect_ = false;
            return result;
        }

        buffer_.clear();
        rawPreview_ = false;
        stackPrefixEnabled_ = false;
        stackJoinPrefix_.clear();
        kinziPending_ = false;
        hasLastTextRect_ = false;

        if (action == EditAction::CommitVisiblePreview) {
            acceptedEnd->Release();
            debugLog("Visible candidate accepted without rewrite; caret restored to word end");
            return S_OK;
        }

        if (action == EditAction::CommitVisiblePreviewAndInsertLiteral) {
            std::wstring literalText;
            appendLiteralWithAutoSpace(literalText, character);
            result = acceptedEnd->SetText(
                editCookie,
                0,
                literalText.c_str(),
                static_cast<LONG>(literalText.size())
            );
            if (SUCCEEDED(result)) {
                result = acceptedEnd->Collapse(editCookie, TF_ANCHOR_END);
            }
            if (SUCCEEDED(result)) {
                TF_SELECTION afterLiteral{};
                afterLiteral.range = acceptedEnd;
                afterLiteral.style.ase = TF_AE_NONE;
                afterLiteral.style.fInterimChar = FALSE;
                result = context->SetSelection(editCookie, 1, &afterLiteral);
            }
            acceptedEnd->Release();

            if (FAILED(result)) {
                debugLogHr("commit visible preview + literal", result);
                return result;
            }

            debugLog("Visible candidate accepted + literal inserted; caret moved after literal");
            return S_OK;
        }

        // CommitVisiblePreviewAndStartNext.
        acceptedEnd->Release();

        char asciiCharacter = static_cast<char>(character);
        if (asciiCharacter >= 'A' && asciiCharacter <= 'Z') {
            asciiCharacter = static_cast<char>(asciiCharacter - 'A' + 'a');
        }
        buffer_.push_back(asciiCharacter);
        rawPreview_ = true;

        result = updateCompositionText(
            editCookie,
            context,
            utf8ToUtf16(buffer_)
        );
        if (SUCCEEDED(result)) {
            debugLog("Visible candidate accepted without rewrite + fresh next raw composition");
        }
        return result;
    }

    case EditAction::CommitCandidateAndStartNext: {
        if (buffer_.empty()) {
            return S_FALSE;
        }
        if (character < L'A' || (character > L'Z' && character < L'a') || character > L'z') {
            return E_INVALIDARG;
        }

        const auto candidates = currentCandidateTexts(candidateIndex + 1);
        if (candidateIndex >= candidates.size() || candidates[candidateIndex].empty()) {
            return E_INVALIDARG;
        }

        // Universal TSF boundary rule (alpha-0.8.9): finish the selected word
        // and begin the next raw word inside ONE read/write edit session.
        // This removes the host-visible gap between EndComposition and the
        // following RequestEditSession where some editors move/rewrite caret.
        HRESULT result = ensureComposition(editCookie, context);
        if (FAILED(result)) {
            return result;
        }

        ITfRange* committedRange = nullptr;
        result = composition_->GetRange(&committedRange);
        if (FAILED(result) || committedRange == nullptr) {
            if (committedRange != nullptr) committedRange->Release();
            releaseCompositionReference();
            return FAILED(result) ? result : E_FAIL;
        }

        const std::wstring& selected = candidates[candidateIndex];
        result = committedRange->SetText(
            editCookie,
            0,
            selected.c_str(),
            static_cast<LONG>(selected.size())
        );
        if (FAILED(result)) {
            committedRange->Release();
            return result;
        }

        ITfRange* nextInsertion = nullptr;
        result = committedRange->Clone(&nextInsertion);
        if (SUCCEEDED(result) && nextInsertion != nullptr) {
            result = nextInsertion->Collapse(editCookie, TF_ANCHOR_END);
        }
        committedRange->Release();
        committedRange = nullptr;
        if (FAILED(result) || nextInsertion == nullptr) {
            if (nextInsertion != nullptr) nextInsertion->Release();
            return FAILED(result) ? result : E_FAIL;
        }

        result = endComposition(editCookie);
        if (FAILED(result)) {
            nextInsertion->Release();
            return result;
        }

        // After EndComposition, explicitly establish the accepted word's end
        // as the insertion point before creating the next composition.
        TF_SELECTION selection{};
        selection.range = nextInsertion;
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        result = context->SetSelection(editCookie, 1, &selection);
        nextInsertion->Release();
        nextInsertion = nullptr;
        if (FAILED(result)) {
            debugLogHr("SetSelection after atomic commit", result);
            buffer_.clear();
            rawPreview_ = false;
            hasLastTextRect_ = false;
            return result;
        }

        buffer_.clear();
        rawPreview_ = false;
        hasLastTextRect_ = false;

        char asciiCharacter = static_cast<char>(character);
        if (asciiCharacter >= 'A' && asciiCharacter <= 'Z') {
            asciiCharacter = static_cast<char>(asciiCharacter - 'A' + 'a');
        }
        buffer_.push_back(asciiCharacter);
        rawPreview_ = true;

        result = updateCompositionText(
            editCookie,
            context,
            utf8ToUtf16(buffer_)
        );
        if (SUCCEEDED(result)) {
            debugLog("Atomic candidate commit + fresh next composition completed");
        }
        return result;
    }

    case EditAction::InsertLiteral: {
        HRESULT result = ensureComposition(editCookie, context);
        if (FAILED(result)) {
            return result;
        }
        std::wstring literalText;
        appendLiteralWithAutoSpace(literalText, character);
        result = updateCompositionText(editCookie, context, literalText);
        if (FAILED(result)) {
            return result;
        }
        result = endComposition(editCookie);
        if (SUCCEEDED(result)) {
            buffer_.clear();
            rawPreview_ = false;
            hasLastTextRect_ = false;
        }
        return result;
    }

    case EditAction::CommitCandidateAndInsertLiteral:
    case EditAction::CommitBestCandidateAndInsertLiteral: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        std::wstring currentVisible;
        if (action == EditAction::CommitCandidateAndInsertLiteral) {
            const auto candidates = currentCandidateTexts(candidateIndex + 1);
            if (candidateIndex >= candidates.size()) {
                return E_INVALIDARG;
            }
            currentVisible = candidates[candidateIndex];
        } else {
            currentVisible = makeBestSegmentText(buffer_);
            if (stackPrefixEnabled_
                && !currentVisible.empty()
                && currentVisible.front() != static_cast<wchar_t>(0x1039)) {
                currentVisible.insert(currentVisible.begin(), static_cast<wchar_t>(0x1039));
            }
        }
        if (currentVisible.empty()) {
            return E_FAIL;
        }
        if (!stackJoinPrefix_.empty()) {
            currentVisible = stackJoinPrefix_ + currentVisible;
        }
        appendLiteralWithAutoSpace(currentVisible, character);
        const HRESULT updateResult = updateCompositionText(editCookie, context, currentVisible);
        if (FAILED(updateResult)) {
            return updateResult;
        }
        const HRESULT endResult = endComposition(editCookie);
        if (SUCCEEDED(endResult)) {
            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;
            hasLastTextRect_ = false;
            debugLog("Candidate committed with literal boundary");
        }
        return endResult;
    }

    case EditAction::CommitOriginalAndInsertLiteral: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        std::wstring visible = utf8ToUtf16(buffer_);
        if (visible.empty()) {
            return E_FAIL;
        }

        if (!stackJoinPrefix_.empty()) {
            visible = stackJoinPrefix_ + visible;
        }

        // R1.12/R1.16 — smart boundary spacing, implemented INSIDE the existing
        // stable raw-commit action.  No Space-key dispatch/candidate logic is
        // changed.
        //
        // Examples:
        //   ဒီ + code + Space       -> ဒီ code[space]
        //   ဒီ[space] + code + Space -> ဒီ code[space]
        //
        // We only inspect the single committed character immediately before
        // the current composition.  If that character exists and is not
        // whitespace, put exactly one ASCII space at the START of the current
        // composition.  Because the space becomes part of the composition
        // text itself, we do not edit committed document text or move the
        // host caret/range outside the composition.
        const bool needsLeadingBoundarySpace =
            needsLeadingRawBoundarySpace(editCookie);

        if (needsLeadingBoundarySpace) {
            visible.insert(
                visible.begin(),
                L' '
            );
        }

        // KEEP the confirmed stable behavior: raw English commit always gets
        // the caller-provided trailing literal.  Space callers pass L' '.
        appendLiteralWithAutoSpace(visible, character);

        const HRESULT updateResult =
            updateCompositionText(
                editCookie,
                context,
                visible
            );

        if (FAILED(updateResult)) {
            return updateResult;
        }

        const HRESULT endResult =
            endComposition(editCookie);

        if (SUCCEEDED(endResult)) {
            lastPreviewCandidate_.clear();
            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;
            hasLastTextRect_ = false;

            debugLog(
                needsLeadingBoundarySpace
                    ? "Raw word committed with one leading boundary space and trailing literal"
                    : "Raw word committed with trailing literal only"
            );
        }

        return endResult;
    }

    case EditAction::CommitCandidate:
    case EditAction::CommitOriginal:
    case EditAction::CommitBestCandidate: {
        if (buffer_.empty()) {
            return S_FALSE;
        }

        std::wstring currentVisible;
        std::wstring selectedForLearning;
        if (action == EditAction::CommitCandidate) {
            const auto candidates = currentCandidateTexts(candidateIndex + 1);
            if (candidateIndex >= candidates.size()) {
                return E_INVALIDARG;
            }
            currentVisible = candidates[candidateIndex];
            selectedForLearning = currentVisible;
        } else if (action == EditAction::CommitOriginal) {
            currentVisible = utf8ToUtf16(buffer_);
        } else {
            currentVisible = makeBestSegmentText(buffer_);
        }

        if (currentVisible.empty()) {
            return E_FAIL;
        }

        if (!stackJoinPrefix_.empty()) {
            currentVisible = stackJoinPrefix_ + currentVisible;
        }

        const std::string finishedRaw = buffer_;

        const HRESULT updateResult = updateCompositionText(
            editCookie,
            context,
            currentVisible
        );
        if (FAILED(updateResult)) {
            return updateResult;
        }

        const HRESULT endResult = endComposition(editCookie);
        if (SUCCEEDED(endResult)) {
            if (!selectedForLearning.empty()) {
                recordUserSelection(finishedRaw, selectedForLearning);
            }
            lastPreviewCandidate_.clear();
            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;
            hasLastTextRect_ = false;
            debugLog("Current word explicitly committed");
        }
        return endResult;
    }

    case EditAction::FinalizeIfSelectionMoved: {
        if (composition_ == nullptr || buffer_.empty()) {
            return S_FALSE;
        }

        // A composition belongs to its original context. If a host reused the
        // service with another context, the old range is stale by definition.
        if (compositionContext_ != context) {
            releaseCompositionReference();
            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;
            hasLastTextRect_ = false;
            return S_OK;
        }

        ITfRange* compositionRange = nullptr;
        HRESULT result = composition_->GetRange(&compositionRange);
        if (FAILED(result) || compositionRange == nullptr) {
            if (compositionRange != nullptr) compositionRange->Release();
            releaseCompositionReference();
            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;
            hasLastTextRect_ = false;
            return S_OK;
        }

        TF_SELECTION selection{};
        ULONG fetched = 0;
        result = context->GetSelection(
            editCookie,
            TF_DEFAULT_SELECTION,
            1,
            &selection,
            &fetched
        );
        if (FAILED(result) || fetched == 0 || selection.range == nullptr) {
            compositionRange->Release();
            if (selection.range != nullptr) selection.range->Release();
            return FAILED(result) ? result : S_FALSE;
        }

        // Keep the composition only when the complete host selection is inside
        // (or exactly at the end of) the composition. A mouse click elsewhere,
        // selecting another paragraph, etc. must detach the old IME state.
        LONG startVsCompositionStart = 0;
        LONG endVsCompositionEnd = 0;
        const HRESULT compareStart = selection.range->CompareStart(
            editCookie,
            compositionRange,
            TF_ANCHOR_START,
            &startVsCompositionStart
        );
        const HRESULT compareEnd = selection.range->CompareEnd(
            editCookie,
            compositionRange,
            TF_ANCHOR_END,
            &endVsCompositionEnd
        );

        const bool selectionInside = SUCCEEDED(compareStart)
            && SUCCEEDED(compareEnd)
            && startVsCompositionStart >= 0
            && endVsCompositionEnd <= 0;

        if (selectionInside) {
            selection.range->Release();
            compositionRange->Release();
            return S_FALSE;
        }

        // Preserve the user's new caret/selection across EndComposition. This is
        // crucial: the old word is accepted where it already is, while the next
        // Backspace/Space/letter must operate at the place the user clicked.
        ITfRange* preservedSelection = nullptr;
        result = selection.range->Clone(&preservedSelection);
        selection.range->Release();
        compositionRange->Release();
        if (FAILED(result) || preservedSelection == nullptr) {
            if (preservedSelection != nullptr) preservedSelection->Release();
            return FAILED(result) ? result : E_FAIL;
        }

        result = endComposition(editCookie);
        if (SUCCEEDED(result)) {
            TF_SELECTION restored{};
            restored.range = preservedSelection;
            restored.style.ase = TF_AE_NONE;
            restored.style.fInterimChar = FALSE;
            const HRESULT selectionResult = context->SetSelection(editCookie, 1, &restored);
            if (FAILED(selectionResult)) {
                debugLogHr("restore host selection after detached composition", selectionResult);
            }

            buffer_.clear();
            rawPreview_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;
            hasLastTextRect_ = false;
            debugLog("Host caret left composition; accepted visible text and detached stale IME state");
        }
        preservedSelection->Release();
        return result;
    }

    case EditAction::Cancel: {
        if (buffer_.empty() && composition_ == nullptr) {
            return S_FALSE;
        }

        if (composition_ != nullptr) {
            const HRESULT updateResult = updateCompositionText(
                editCookie,
                context,
                L""
            );
            if (FAILED(updateResult)) {
                return updateResult;
            }
        }

        const HRESULT endResult = endComposition(editCookie);
        if (SUCCEEDED(endResult)) {
            buffer_.clear();
            lastPreviewCandidate_.clear();
            rawPreview_ = false;
            kinziPending_ = false;
            stackPrefixEnabled_ = false;
            stackJoinPrefix_.clear();
            kinziPending_ = false;
            hasLastTextRect_ = false;
            debugLog(
                "Composition cancelled; "
                "kinzi/stack state fully cleared"
            );
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
    if (context == nullptr) {
        return E_POINTER;
    }

    // A TSF composition belongs to exactly one context. Never reuse a
    // composition object after the host moves focus to another editor/context.
    // Some apps expose the change differently, so this is enforced here instead
    // of relying only on focus callbacks.
    if (composition_ != nullptr) {
        if (compositionContext_ == context) {
            return S_OK;
        }
        debugLog("Composition context changed; discarding stale composition reference before fresh insertion");
        releaseCompositionReference();
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

    compositionContext_ = context;
    compositionContext_->AddRef();

    debugLog("Composition started with owning context");
    return S_OK;
}

HRESULT CompositionManager::updateCompositionText(
    TfEditCookie editCookie,
    ITfContext* context,
    const std::wstring& text
) {
    for (int attempt = 0; attempt < 2; ++attempt) {
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

        if (FAILED(result) || range == nullptr) {
            if (range != nullptr) {
                range->Release();
            }

            const HRESULT rangeResult = FAILED(result) ? result : E_FAIL;
            debugLogHr("ITfComposition::GetRange", rangeResult);

            // TSF can terminate a composition asynchronously when focus or host
            // state changes.  The COM object is still safe to Release because we
            // own a reference, but its range is no longer usable.  Drop it and,
            // once, start a fresh composition instead of swallowing every later
            // key forever.
            releaseCompositionReference();

            if (attempt == 0) {
                debugLog("Discarded stale TSF composition; retrying with a fresh composition");
                continue;
            }
            return rangeResult;
        }

        // This is normal IME composition text creation/replacement, not a
        // correction of already committed document text.
        result =
            range->SetText(
                editCookie,
                0,
                text.c_str(),
                static_cast<LONG>(text.size())
            );

        if (FAILED(result)) {
            debugLogHr("ITfRange::SetText", result);
        } else {
            // Universal TSF caret rule: after replacing composition text, make
            // the document selection explicitly follow the END of that same
            // composition range. This prevents hosts that preserve an old caret
            // from inserting the next composition before the previous word.
            const HRESULT caretResult = placeCaretAtCompositionEnd(
                editCookie,
                context,
                range
            );
            if (FAILED(caretResult)) {
                debugLogHr("placeCaretAtCompositionEnd", caretResult);
                // The text update itself succeeded. Do not fail the keystroke
                // merely because a host rejected an explicit selection update.
            }

            // Use TSF layout information from the active composition range for
            // popup placement. No process/app-specific coordinates are used.
            ITfContextView* view = nullptr;
            if (SUCCEEDED(context->GetActiveView(&view)) && view != nullptr) {
                RECT rect{};
                BOOL clipped = FALSE;
                const HRESULT extentResult = view->GetTextExt(
                    editCookie,
                    range,
                    &rect,
                    &clipped
                );
                if (SUCCEEDED(extentResult)) {
                    lastTextRect_ = rect;
                    hasLastTextRect_ = true;
                }
                view->Release();
            }
        }

        range->Release();
        return result;
    }

    return E_FAIL;
}

HRESULT CompositionManager::placeCaretAtCompositionEnd(
    TfEditCookie editCookie,
    ITfContext* context,
    ITfRange* compositionRange
) {
    if (context == nullptr || compositionRange == nullptr) {
        return E_POINTER;
    }

    ITfRange* caretRange = nullptr;
    HRESULT result = compositionRange->Clone(&caretRange);
    if (FAILED(result) || caretRange == nullptr) {
        if (caretRange != nullptr) {
            caretRange->Release();
        }
        return FAILED(result) ? result : E_FAIL;
    }

    result = caretRange->Collapse(editCookie, TF_ANCHOR_END);
    if (SUCCEEDED(result)) {
        TF_SELECTION selection{};
        selection.range = caretRange;
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        result = context->SetSelection(editCookie, 1, &selection);
    }

    caretRange->Release();
    return result;
}

HRESULT CompositionManager::endComposition(
    TfEditCookie editCookie
) {
    if (composition_ == nullptr) {
        return S_OK;
    }

    ITfComposition* compositionToEnd = composition_;
    composition_ = nullptr;

    ITfContext* contextToRelease = compositionContext_;
    compositionContext_ = nullptr;

    const HRESULT result =
        compositionToEnd->EndComposition(editCookie);

    compositionToEnd->Release();
    if (contextToRelease != nullptr) {
        contextToRelease->Release();
    }

    if (FAILED(result)) {
        debugLogHr("ITfComposition::EndComposition", result);
    } else {
        hasLastTextRect_ = false;
    }
    return result;
}

std::wstring CompositionManager::lockedPrefixText() const {
    // alpha-0.6.3: older words are committed as separate TSF compositions.
    return {};
}

std::wstring CompositionManager::makeBestSegmentText(const std::string& buffer) const {
    if (buffer.empty()) {
        return {};
    }

    if (converter_ != nullptr) {
        const auto candidates = converter_->getContinuousCandidates(buffer, 1);
        if (!candidates.empty()
            && !candidates.front().burmese.empty()
            && candidates.front().burmese != buffer) {
            const std::wstring converted = utf8ToUtf16(candidates.front().burmese);
            if (!converted.empty()) {
                return converted;
            }
        }
    }

    // alpha-0.8: do not invent a Burmese onset for an incomplete/unknown word.
    // Keep the Roman text visible until a validated candidate exists; as soon
    // as one exists, the inline composition automatically changes to candidate #1.
    return utf8ToUtf16(buffer);
}

std::wstring CompositionManager::makeRollingPreviewText(const std::string& buffer) const {
    if (buffer.empty()) {
        return {};
    }

    if (converter_ == nullptr) {
        return utf8ToUtf16(buffer);
    }

    const std::size_t split = converter_->findRollingSplit(buffer);
    if (split == 0 || split >= buffer.size()) {
        return makeBestSegmentText(buffer);
    }

    const std::string leftRaw = buffer.substr(0, split);
    const std::string rightRaw = buffer.substr(split);
    const std::wstring left = makeBestSegmentText(leftRaw);
    const std::wstring right = makeBestSegmentText(rightRaw);
    if (left.empty() || right.empty()) {
        return makeBestSegmentText(buffer);
    }
    return left + right;
}

std::wstring CompositionManager::makeCommittedText(
    bool useBestCandidate
) const {
    (void)useBestCandidate;
    return makeBestSegmentText(buffer_);
}

std::wstring CompositionManager::makeCandidateText(
    std::size_t candidateIndex
) const {
    const auto candidates = currentCandidateTexts(candidateIndex + 1);
    if (candidateIndex >= candidates.size()) {
        return {};
    }
    return candidates[candidateIndex];
}

std::wstring CompositionManager::makeBestVisibleText(
    const std::string& buffer
) const {
    return makeBestSegmentText(buffer);
}



} // namespace myanglish::ime
