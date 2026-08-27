#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <Windows.h>

namespace myanglish::ime {

class CandidateWindow {
public:
    using ForegroundLossCallback = void (*)(void*);
    using ManageWordsCallback = void (*)(void*);
    using SelectionChangedCallback = void (*)(void*, std::size_t);
    using CandidateCommitCallback = void (*)(void*, std::size_t);

    CandidateWindow() = default;
    ~CandidateWindow();

    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    bool show(
        const std::vector<std::wstring>& candidates,
        std::size_t selectedIndex = 0,
        const RECT* textRect = nullptr
    );
    void hide() noexcept;
    bool isVisible() const noexcept;
    bool consumeForegroundLoss() noexcept;
    void setForegroundLossCallback(ForegroundLossCallback callback, void* context) noexcept;
    void setManageWordsCallback(ManageWordsCallback callback, void* context) noexcept;
    void setSelectionChangedCallback(SelectionChangedCallback callback, void* context) noexcept;
    void setCandidateCommitCallback(CandidateCommitCallback callback, void* context) noexcept;

    std::size_t selectedIndex() const noexcept;
    std::size_t candidateCount() const noexcept;
    void setSelection(std::size_t index) noexcept;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool ensureWindow();
    void resizeAndPosition();
    void paint();
    void hideForForegroundLoss() noexcept;
    bool candidateIndexFromPoint(POINT point, std::size_t& index) const noexcept;

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    std::vector<std::wstring> candidates_;
    std::size_t selectedIndex_ = 0;
    RECT textRect_{};
    bool hasTextRect_ = false;
    HWND foregroundRoot_ = nullptr;
    bool foregroundLossPending_ = false;
    ForegroundLossCallback foregroundLossCallback_ = nullptr;
    ManageWordsCallback manageWordsCallback_ = nullptr;
    SelectionChangedCallback selectionChangedCallback_ = nullptr;
    CandidateCommitCallback candidateCommitCallback_ = nullptr;
    void* foregroundLossContext_ = nullptr;
    void* manageWordsContext_ = nullptr;
    void* selectionChangedContext_ = nullptr;
    void* candidateCommitContext_ = nullptr;

    RECT lastCaretScreenRect_{};
    bool hasLastCaretScreenRect_ = false;
    bool externalMouseDown_ = false;
};

} // namespace myanglish::ime
