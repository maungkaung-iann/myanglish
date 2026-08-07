#include "CandidateWindow.h"

#include "Globals.h"

#include <algorithm>
#include <array>
#include <string>

namespace myanglish::ime {

namespace {

constexpr wchar_t kCandidateWindowClass[] = L"MyanglishIMECandidateWindow";
constexpr int kHorizontalPadding = 12;
constexpr int kVerticalPadding = 7;
constexpr int kMinimumWidth = 170;
constexpr int kMaximumWidth = 520;
constexpr int kLineHeight = 34;

bool registerCandidateWindowClass() {
    static bool attempted = false;
    static bool registered = false;

    if (attempted) {
        return registered;
    }

    attempted = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CandidateWindow::windowProc;
    wc.hInstance = moduleHandle();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kCandidateWindowClass;

    const ATOM atom = RegisterClassExW(&wc);
    if (atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        registered = true;
    }

    return registered;
}

POINT candidateAnchorPoint() {
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);

    if (GetGUIThreadInfo(0, &gui) && gui.hwndCaret != nullptr) {
        POINT point{gui.rcCaret.left, gui.rcCaret.bottom + 4};
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
        WS_POPUP | WS_BORDER,
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
        -22,
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

bool CandidateWindow::show(const std::vector<std::wstring>& candidates, std::size_t selectedIndex) {
    if (candidates.empty()) {
        hide();
        return false;
    }

    if (!ensureWindow()) {
        return false;
    }

    candidates_ = candidates;
    selectedIndex_ = (std::min)(selectedIndex, candidates_.size() - 1);
    resizeAndPosition();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateWindow(hwnd_);
    return true;
}

void CandidateWindow::hide() noexcept {
    candidates_.clear();
    selectedIndex_ = 0;

    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
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
            const std::wstring line = std::to_wstring(index + 1) + L".  " + candidates_[index];
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

    const int width = std::clamp(contentWidth + (kHorizontalPadding * 2), kMinimumWidth, kMaximumWidth);
    const int height = static_cast<int>(candidates_.size()) * kLineHeight;

    POINT anchor = candidateAnchorPoint();

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
            anchor.y = (std::max)(monitorInfo.rcWork.top, anchor.y - height - 26);
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
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));

    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        RECT row{
            0,
            static_cast<LONG>(index * kLineHeight),
            client.right,
            static_cast<LONG>((index + 1) * kLineHeight)
        };

        const bool selected = index == selectedIndex_;
        if (selected) {
            FillRect(dc, &row, GetSysColorBrush(COLOR_HIGHLIGHT));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }

        RECT textRect = row;
        textRect.left += kHorizontalPadding;
        textRect.right -= kHorizontalPadding;
        textRect.top += kVerticalPadding / 2;

        const std::wstring line = std::to_wstring(index + 1) + L".  " + candidates_[index];
        DrawTextW(
            dc,
            line.c_str(),
            static_cast<int>(line.size()),
            &textRect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS
        );
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

