#include "CandidateWindow.h"

#include "Globals.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <windowsx.h>

namespace myanglish::ime {

namespace {

constexpr wchar_t kCandidateWindowClass[] = L"MyanglishIMECandidateWindow";
constexpr int kHorizontalPadding = 14;
constexpr int kVerticalPadding = 7;
constexpr int kMinimumWidth = 176;
constexpr int kMaximumWidth = 520;
constexpr int kLineHeight = 38;
constexpr int kCornerRadius = 14;
constexpr int kOuterMargin = 5;
constexpr int kManageFooterHeight = 38;
constexpr UINT_PTR kForegroundGuardTimerId = 0xA085;
constexpr UINT kForegroundGuardIntervalMs = 75;

constexpr COLORREF kBackground = RGB(250, 250, 250);
constexpr COLORREF kBorder = RGB(218, 218, 222);
constexpr COLORREF kText = RGB(28, 28, 30);
constexpr COLORREF kMutedText = RGB(110, 110, 115);
constexpr COLORREF kSelectedBackground = RGB(230, 230, 235);

bool registerCandidateWindowClass() {
    static bool attempted = false;
    static bool registered = false;

    if (attempted) {
        return registered;
    }

    attempted = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = CandidateWindow::windowProc;
    wc.hInstance = moduleHandle();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCandidateWindowClass;

    const ATOM atom = RegisterClassExW(&wc);
    if (atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        registered = true;
    }

    return registered;
}


bool currentHostCaretScreenRect(HWND expectedRoot, RECT& rect) {
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }

    HWND root = GetAncestor(foreground, GA_ROOT);
    if (root == nullptr) {
        root = foreground;
    }

    if (expectedRoot != nullptr && root != expectedRoot) {
        return false;
    }

    const DWORD threadId =
        GetWindowThreadProcessId(foreground, nullptr);

    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);

    if (threadId == 0
        || !GetGUIThreadInfo(threadId, &gui)
        || gui.hwndCaret == nullptr) {
        return false;
    }

    POINT topLeft{
        gui.rcCaret.left,
        gui.rcCaret.top
    };

    POINT bottomRight{
        gui.rcCaret.right,
        gui.rcCaret.bottom
    };

    if (!ClientToScreen(gui.hwndCaret, &topLeft)
        || !ClientToScreen(gui.hwndCaret, &bottomRight)) {
        return false;
    }

    rect.left = topLeft.x;
    rect.top = topLeft.y;
    rect.right = bottomRight.x;
    rect.bottom = bottomRight.y;
    return true;
}

bool rectMoved(const RECT& a, const RECT& b) {
    constexpr LONG kTolerance = 2;

    return std::abs(a.left - b.left) > kTolerance
        || std::abs(a.top - b.top) > kTolerance
        || std::abs(a.right - b.right) > kTolerance
        || std::abs(a.bottom - b.bottom) > kTolerance;
}

POINT candidateAnchorPoint() {
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);

    if (GetGUIThreadInfo(0, &gui) && gui.hwndCaret != nullptr) {
        POINT point{gui.rcCaret.left, gui.rcCaret.bottom + 7};
        if (ClientToScreen(gui.hwndCaret, &point)) {
            return point;
        }
    }

    POINT point{};
    if (!GetCursorPos(&point)) {
        point.x = 100;
        point.y = 100;
    }
    point.y += 18;
    return point;
}

} // namespace

CandidateWindow::~CandidateWindow() {
    hide();

    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }

    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool CandidateWindow::ensureWindow() {
    if (hwnd_ != nullptr) {
        return true;
    }

    if (!registerCandidateWindowClass()) {
        debugLog("CandidateWindow: class registration failed");
        return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kCandidateWindowClass,
        L"",
        WS_POPUP,
        0,
        0,
        kMinimumWidth,
        kLineHeight,
        nullptr,
        nullptr,
        moduleHandle(),
        this
    );

    if (hwnd_ == nullptr) {
        debugLog("CandidateWindow: CreateWindowExW failed");
        return false;
    }

    font_ = CreateFontW(
        -21,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Myanmar Text"
    );

    return true;
}

bool CandidateWindow::show(
    const std::vector<std::wstring>& candidates,
    std::size_t selectedIndex,
    const RECT* textRect
) {
    if (candidates.empty()) {
        hide();
        return false;
    }

    if (!ensureWindow()) {
        return false;
    }

    HWND foreground = GetForegroundWindow();
    foregroundRoot_ = foreground != nullptr ? GetAncestor(foreground, GA_ROOT) : nullptr;
    if (foregroundRoot_ == nullptr) {
        foregroundRoot_ = foreground;
    }
    foregroundLossPending_ = false;
    externalMouseDown_ = false;
    hasLastCaretScreenRect_ =
        currentHostCaretScreenRect(
            foregroundRoot_,
            lastCaretScreenRect_
        );

    candidates_ = candidates;
    selectedIndex_ = (std::min)(selectedIndex, candidates_.size() - 1);
    if (textRect != nullptr) {
        textRect_ = *textRect;
        hasTextRect_ = true;
    } else {
        hasTextRect_ = false;
    }
    resizeAndPosition();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetTimer(hwnd_, kForegroundGuardTimerId, kForegroundGuardIntervalMs, nullptr);
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateWindow(hwnd_);
    return true;
}

void CandidateWindow::hide() noexcept {
    candidates_.clear();
    selectedIndex_ = 0;
    hasTextRect_ = false;
    foregroundRoot_ = nullptr;
    hasLastCaretScreenRect_ = false;
    externalMouseDown_ = false;

    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kForegroundGuardTimerId);
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void CandidateWindow::hideForForegroundLoss() noexcept {
    foregroundLossPending_ = true;
    hide();

    // The popup owns the independent foreground detector, but TextService owns
    // the composition/candidate state. Notify it immediately when possible so
    // stale selection state is cleared at the same moment the popup disappears.
    if (foregroundLossCallback_ != nullptr) {
        foregroundLossPending_ = false;
        foregroundLossCallback_(foregroundLossContext_);
    }
}

bool CandidateWindow::consumeForegroundLoss() noexcept {
    const bool lost = foregroundLossPending_;
    foregroundLossPending_ = false;
    return lost;
}

void CandidateWindow::setForegroundLossCallback(
    ForegroundLossCallback callback,
    void* context
) noexcept {
    foregroundLossCallback_ = callback;
    foregroundLossContext_ = context;
}

void CandidateWindow::setManageWordsCallback(
    ManageWordsCallback callback,
    void* context
) noexcept {
    manageWordsCallback_ = callback;
    manageWordsContext_ = context;
}


void CandidateWindow::setSelectionChangedCallback(
    SelectionChangedCallback callback,
    void* context
) noexcept {
    selectionChangedCallback_ = callback;
    selectionChangedContext_ = context;
}

void CandidateWindow::setCandidateCommitCallback(
    CandidateCommitCallback callback,
    void* context
) noexcept {
    candidateCommitCallback_ = callback;
    candidateCommitContext_ = context;
}

bool CandidateWindow::candidateIndexFromPoint(
    POINT point,
    std::size_t& index
) const noexcept {
    if (candidates_.empty()) {
        return false;
    }

    const int y = point.y;
    const int candidateTop = kOuterMargin;
    const int candidateBottom =
        kOuterMargin
        + static_cast<int>(candidates_.size()) * kLineHeight;

    if (y < candidateTop || y >= candidateBottom) {
        return false;
    }

    const int rawIndex =
        (y - candidateTop) / kLineHeight;

    if (rawIndex < 0
        || static_cast<std::size_t>(rawIndex)
            >= candidates_.size()) {
        return false;
    }

    index = static_cast<std::size_t>(rawIndex);
    return true;
}

bool CandidateWindow::isVisible() const noexcept {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE && !candidates_.empty();
}

std::size_t CandidateWindow::selectedIndex() const noexcept {
    return selectedIndex_;
}

std::size_t CandidateWindow::candidateCount() const noexcept {
    return candidates_.size();
}

void CandidateWindow::setSelection(std::size_t index) noexcept {
    if (candidates_.empty()) {
        selectedIndex_ = 0;
        return;
    }

    selectedIndex_ = (std::min)(index, candidates_.size() - 1);
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
}

void CandidateWindow::resizeAndPosition() {
    if (hwnd_ == nullptr || candidates_.empty()) {
        return;
    }

    HDC dc = GetDC(hwnd_);
    HFONT oldFont = nullptr;
    if (dc != nullptr && font_ != nullptr) {
        oldFont = static_cast<HFONT>(SelectObject(dc, font_));
    }

    int contentWidth = kMinimumWidth - (kHorizontalPadding * 2);
    if (dc != nullptr) {
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            const std::wstring line = std::to_wstring(index + 1) + L"   " + candidates_[index];
            SIZE size{};
            if (GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &size)) {
                contentWidth = (std::max)(contentWidth, static_cast<int>(size.cx));
            }
        }
    }

    if (dc != nullptr && oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }
    if (dc != nullptr) {
        ReleaseDC(hwnd_, dc);
    }

    const int width = std::clamp(
        contentWidth + (kHorizontalPadding * 2) + 16,
        kMinimumWidth,
        kMaximumWidth
    );
    const int height = static_cast<int>(candidates_.size()) * kLineHeight
        + (kOuterMargin * 2) + kManageFooterHeight;

    POINT anchor{};
    if (hasTextRect_) {
        anchor.x = textRect_.left;
        anchor.y = textRect_.bottom + 6;
    } else {
        anchor = candidateAnchorPoint();
    }

    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        if (anchor.x + width > monitorInfo.rcWork.right) {
            anchor.x = monitorInfo.rcWork.right - width;
        }
        if (anchor.x < monitorInfo.rcWork.left) {
            anchor.x = monitorInfo.rcWork.left;
        }
        if (anchor.y + height > monitorInfo.rcWork.bottom) {
            if (hasTextRect_) {
                anchor.y = (std::max)(monitorInfo.rcWork.top, textRect_.top - height - 6);
            } else {
                anchor.y = (std::max)(monitorInfo.rcWork.top, anchor.y - height - 28);
            }
        }
    }

    // Win32 fallback rounded corners. It works on both Windows 10 and 11 and
    // does not require a DWM dependency.
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, kCornerRadius, kCornerRadius);
    if (region != nullptr) {
        if (SetWindowRgn(hwnd_, region, TRUE) == 0) {
            DeleteObject(region);
        }
    }

    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        anchor.x,
        anchor.y,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}

void CandidateWindow::paint() {
    if (hwnd_ == nullptr) {
        return;
    }

    PAINTSTRUCT paintStruct{};
    HDC dc = BeginPaint(hwnd_, &paintStruct);
    if (dc == nullptr) {
        return;
    }

    HFONT oldFont = nullptr;
    if (font_ != nullptr) {
        oldFont = static_cast<HFONT>(SelectObject(dc, font_));
    }

    SetBkMode(dc, TRANSPARENT);

    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH backgroundBrush = CreateSolidBrush(kBackground);
    HBRUSH selectedBrush = CreateSolidBrush(kSelectedBackground);
    HPEN borderPen = CreatePen(PS_SOLID, 1, kBorder);

    if (backgroundBrush != nullptr) {
        FillRect(dc, &client, backgroundBrush);
    }

    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        RECT row{
            kOuterMargin,
            static_cast<LONG>(kOuterMargin + index * kLineHeight),
            client.right - kOuterMargin,
            static_cast<LONG>(kOuterMargin + (index + 1) * kLineHeight)
        };

        const bool selected = index == selectedIndex_;
        if (selected && selectedBrush != nullptr) {
            HGDIOBJ oldBrush = SelectObject(dc, selectedBrush);
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, row.left, row.top + 2, row.right, row.bottom - 2, 10, 10);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
        }

        SetTextColor(dc, selected ? kText : kText);

        RECT numberRect = row;
        numberRect.left += kHorizontalPadding;
        numberRect.right = numberRect.left + 24;
        SetTextColor(dc, kMutedText);
        const std::wstring number = std::to_wstring(index + 1);
        DrawTextW(
            dc,
            number.c_str(),
            static_cast<int>(number.size()),
            &numberRect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
        );

        RECT textRect = row;
        textRect.left += kHorizontalPadding + 30;
        textRect.right -= kHorizontalPadding;
        SetTextColor(dc, kText);
        DrawTextW(
            dc,
            candidates_[index].c_str(),
            static_cast<int>(candidates_[index].size()),
            &textRect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS
        );
    }


    {
        const LONG footerTop =
            kOuterMargin + static_cast<LONG>(candidates_.size()) * kLineHeight;

        HPEN divider = CreatePen(PS_SOLID, 1, kBorder);
        if (divider != nullptr) {
            HGDIOBJ oldPen = SelectObject(dc, divider);
            MoveToEx(dc, kOuterMargin + 4, footerTop + 2, nullptr);
            LineTo(dc, client.right - kOuterMargin - 4, footerTop + 2);
            SelectObject(dc, oldPen);
            DeleteObject(divider);
        }

        RECT footerRect{
            kOuterMargin + kHorizontalPadding,
            footerTop + 3,
            client.right - kOuterMargin - kHorizontalPadding,
            footerTop + kManageFooterHeight
        };

        SetTextColor(dc, RGB(35, 95, 180));
        const wchar_t footerText[] = L"+ Add / manage words...";
        DrawTextW(
            dc, footerText,
            static_cast<int>(std::size(footerText) - 1),
            &footerRect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
        );
    }

    if (borderPen != nullptr) {
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, 0, 0, client.right, client.bottom, kCornerRadius, kCornerRadius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
    }

    if (backgroundBrush != nullptr) {
        DeleteObject(backgroundBrush);
    }
    if (selectedBrush != nullptr) {
        DeleteObject(selectedBrush);
    }
    if (borderPen != nullptr) {
        DeleteObject(borderPen);
    }

    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }

    EndPaint(hwnd_, &paintStruct);
}

LRESULT CALLBACK CandidateWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    CandidateWindow* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<CandidateWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (self != nullptr && wParam == kForegroundGuardTimerId) {
            HWND foreground = GetForegroundWindow();
            HWND currentRoot =
                foreground != nullptr
                    ? GetAncestor(foreground, GA_ROOT)
                    : nullptr;

            if (currentRoot == nullptr) {
                currentRoot = foreground;
            }

            if (self->foregroundRoot_ != nullptr
                && currentRoot != self->foregroundRoot_) {
                debugLog(
                    "CandidateWindow: foreground window changed; "
                    "commit selected candidate and close popup"
                );
                self->hideForForegroundLoss();
                return 0;
            }

            // Romaji-IME style host-caret guard:
            // candidate preview itself may change text width, so continuously
            // refresh the baseline while the mouse is idle. Only treat a caret
            // move as user navigation when an OUTSIDE mouse press occurred.
            const bool leftDown =
                (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

            POINT cursor{};
            bool insidePopup = false;

            if (GetCursorPos(&cursor)) {
                RECT popupRect{};
                if (GetWindowRect(hwnd, &popupRect)) {
                    insidePopup =
                        PtInRect(&popupRect, cursor) != FALSE;
                }
            }

            if (leftDown) {
                if (!insidePopup) {
                    self->externalMouseDown_ = true;
                }
                return 0;
            }

            RECT caretRect{};
            const bool hasCaret =
                currentHostCaretScreenRect(
                    self->foregroundRoot_,
                    caretRect
                );

            if (self->externalMouseDown_) {
                self->externalMouseDown_ = false;

                if (hasCaret
                    && self->hasLastCaretScreenRect_
                    && rectMoved(
                        self->lastCaretScreenRect_,
                        caretRect
                    )) {
                    debugLog(
                        "CandidateWindow: host caret moved by mouse; "
                        "commit selected candidate and close popup"
                    );
                    self->hideForForegroundLoss();
                    return 0;
                }
            }

            if (hasCaret) {
                self->lastCaretScreenRect_ = caretRect;
                self->hasLastCaretScreenRect_ = true;
            }

            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        // Hover only. Do not change the selected candidate or preview.
        // Candidate selection changes only after an actual mouse click.
        return 0;

    case WM_LBUTTONDOWN:
        // Keep the host editor focused. The popup is WS_EX_NOACTIVATE,
        // so mouse selection behaves like a standard IME candidate UI.
        return 0;

    case WM_LBUTTONUP:
        if (self != nullptr) {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            std::size_t index = 0;

            if (self->candidateIndexFromPoint(
                    point,
                    index
                )) {
                self->setSelection(index);

                if (self->selectionChangedCallback_
                    != nullptr) {
                    self->selectionChangedCallback_(
                        self->selectionChangedContext_,
                        index
                    );
                }

                if (self->candidateCommitCallback_
                    != nullptr) {
                    self->candidateCommitCallback_(
                        self->candidateCommitContext_,
                        index
                    );
                }

                return 0;
            }

            const int y = point.y;
            const int footerTop =
                kOuterMargin
                + static_cast<int>(
                    self->candidates_.size()
                ) * kLineHeight;

            if (y >= footerTop) {
                ManageWordsCallback callback =
                    self->manageWordsCallback_;

                void* callbackContext =
                    self->manageWordsContext_;

                self->hide();

                if (callback != nullptr) {
                    callback(callbackContext);
                }

                return 0;
            }
        }
        break;

    case WM_PAINT:
        if (self != nullptr) {
            self->paint();
            return 0;
        }
        break;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace myanglish::ime
