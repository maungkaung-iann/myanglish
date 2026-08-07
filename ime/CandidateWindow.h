#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <Windows.h>

namespace myanglish::ime {

class CandidateWindow {
public:
    CandidateWindow() = default;
    ~CandidateWindow();

    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    bool show(const std::vector<std::wstring>& candidates, std::size_t selectedIndex = 0);
    void hide() noexcept;
    bool isVisible() const noexcept;

    std::size_t selectedIndex() const noexcept;
    std::size_t candidateCount() const noexcept;
    void setSelection(std::size_t index) noexcept;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool ensureWindow();
    void resizeAndPosition();
    void paint();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    std::vector<std::wstring> candidates_;
    std::size_t selectedIndex_ = 0;
};

} // namespace myanglish::ime
