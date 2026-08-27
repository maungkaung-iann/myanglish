#pragma once

#include "Globals.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

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
    bool lastTextRect(RECT& rect) const noexcept;
    void setClientId(TfClientId clientId) noexcept;
    void setStackPrefixEnabled(bool enabled) noexcept;

    // Candidate texts are for the current editable segment only. The visible
    // TSF composition can contain earlier manually selected locked segments.
    std::vector<std::wstring> currentCandidateTexts(std::size_t limit = 9) const;
    bool isCurrentLoanword() const noexcept;
    bool isRawLoanwordCandidate(std::size_t candidateIndex) const noexcept;

    HRESULT insertCharacter(ITfContext* context, wchar_t character);
    HRESULT deleteBackspace(ITfContext* context);
    HRESULT previewCandidate(ITfContext* context, std::size_t candidateIndex);
    HRESULT restoreOriginalPreview(ITfContext* context);
    HRESULT toggleRawWord(ITfContext* context);
    HRESULT lockCandidate(ITfContext* context, std::size_t candidateIndex);
    HRESULT commitCandidate(ITfContext* context, std::size_t candidateIndex);

    // Alpha 0.10.0: Ctrl+Enter stack shortcut. The character immediately
    // before the active Roman composition is folded into one edit range, then
    // exactly one U+1039 VIRAMA and the best current Burmese candidate are
    // written and committed. Example: လိမ + mar + Ctrl+Enter -> လိမ္မာ.
    HRESULT commitStackShortcut(ITfContext* context, std::size_t candidateIndex);
    HRESULT commitKinziShortcut(ITfContext* context, std::size_t candidateIndex);
    HRESULT beginKinziPending(ITfContext* context);

    // Alpha 0.9.9: begin a stacked syllable WITHOUT ending the current
    // composition. The previous Burmese candidate and the new virama-led
    // syllable stay inside one TSF composition range.
    HRESULT beginStackJoin(ITfContext* context, std::size_t previousCandidateIndex, wchar_t character);

    // Alpha 0.9.8: once a candidate is already visible as TSF composition text,
    // accept that exact preview without rewriting it. This is important for a
    // leading Myanmar virama, which some text stores can visually attach to the
    // preceding committed syllable while the composition is still active.
    HRESULT commitVisiblePreview(ITfContext* context);
    HRESULT commitVisiblePreviewAndStartNext(ITfContext* context, wchar_t character);
    HRESULT commitVisiblePreviewAndInsertLiteral(ITfContext* context, wchar_t literal);

    HRESULT commitCandidateAndStartNext(ITfContext* context, std::size_t candidateIndex, wchar_t character);
    HRESULT commitCandidateAndInsertLiteral(ITfContext* context, std::size_t candidateIndex, wchar_t literal);
    HRESULT commitBestCandidateAndInsertLiteral(ITfContext* context, wchar_t literal);
    HRESULT insertLiteral(ITfContext* context, wchar_t literal);
    HRESULT commitOriginal(ITfContext* context);
    HRESULT commitOriginalAndInsertLiteral(ITfContext* context, wchar_t literal);
    HRESULT commitBestCandidate(ITfContext* context);
    HRESULT cancel(ITfContext* context);
    // Finalize a composition if the host selection/caret has moved outside it.
    // Returns S_OK when stale composition state was finalized, S_FALSE when the
    // current selection still belongs to the composition.
    HRESULT finalizeIfSelectionMoved(ITfContext* context);
    void clearWithoutContext() noexcept;

private:
    class EditSession;

public:
    enum class EditAction {
        InsertCharacter,
        DeleteBackspace,
        PreviewCandidate,
        RestoreOriginalPreview,
        ToggleRawWord,
        LockCandidate,
        CommitCandidate,
        CommitStackShortcut,
        CommitKinziShortcut,
        BeginKinziPending,
        BeginStackJoin,
        CommitVisiblePreview,
        CommitVisiblePreviewAndStartNext,
        CommitVisiblePreviewAndInsertLiteral,
        CommitCandidateAndStartNext,
        CommitCandidateAndInsertLiteral,
        CommitBestCandidateAndInsertLiteral,
        InsertLiteral,
        CommitOriginal,
        CommitOriginalAndInsertLiteral,
        CommitBestCandidate,
        Cancel,
        FinalizeIfSelectionMoved,
    };

    HRESULT executeEdit(
        TfEditCookie ec,
        ITfContext* context,
        EditAction action,
        wchar_t character,
        std::size_t candidateIndex
    );

    HRESULT requestEdit(
        ITfContext* context,
        EditAction action,
        wchar_t character = 0,
        std::size_t candidateIndex = 0
    );

    HRESULT ensureComposition(TfEditCookie ec, ITfContext* context);
    HRESULT updateCompositionText(TfEditCookie ec, ITfContext* context, const std::wstring& text);
    HRESULT endComposition(TfEditCookie ec);
    HRESULT placeCaretAtCompositionEnd(TfEditCookie ec, ITfContext* context, ITfRange* compositionRange);
    HRESULT applyCandidateIndicator(TfEditCookie ec, ITfContext* context, bool visible);
    bool needsLeadingRawBoundarySpace(TfEditCookie ec) const;
    void releaseCompositionReference() noexcept;

    std::wstring lockedPrefixText() const;
    std::wstring makeCommittedText(bool useBestCandidate) const;
    std::wstring makeCandidateText(std::size_t candidateIndex) const;
    std::wstring makeBestSegmentText(const std::string& buffer) const;
    std::wstring makeRollingPreviewText(const std::string& buffer) const;
    std::wstring makeBestVisibleText(const std::string& buffer) const;

    struct UserChoiceStats {
        int count = 0;
        std::int64_t lastUsed = 0;
    };

    void loadLoanwordInputs();
    void loadUserHistory();
    void recordUserSelection(const std::string& raw, const std::wstring& candidate);
    int userChoiceScore(const std::string& raw, const std::wstring& candidate) const;

    std::filesystem::path dataRoot_;
    std::unordered_set<std::string> loanwordInputs_;
    std::unique_ptr<myanglish::MyanglishConverter> converter_;
    std::unordered_map<std::string, std::unordered_map<std::wstring, UserChoiceStats>> userHistory_;
    std::wstring lastPreviewCandidate_;
    std::string buffer_;
    bool rawPreview_ = false;
    bool stackPrefixEnabled_ = false;
    // Full Burmese prefix that remains inside the SAME composition while
    // the user types the next stacked syllable. This prevents U+1039 from
    // living at the start of a separate composition.
    std::wstring stackJoinPrefix_;
    bool kinziPending_ = false;
    ITfComposition* composition_ = nullptr;
    ITfContext* compositionContext_ = nullptr;
    RECT lastTextRect_{};
    bool hasLastTextRect_ = false;
    TfClientId clientId_ = TF_CLIENTID_NULL;
};

} // namespace myanglish::ime
