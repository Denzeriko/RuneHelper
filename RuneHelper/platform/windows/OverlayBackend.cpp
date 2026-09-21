#include "platform/OverlayBackend.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <map>
#include <utility>

#include <string>

#include "core/Logger.h"
#include "ui/OverlayState.h"

namespace
{
#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

std::wstring ToWide(const std::string& text)
{
    if (text.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);

    if (size <= 0)
        return {};

    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);

    return wide;
}

constexpr COLORREF kTransparentKey = RGB(0, 0, 0);
constexpr COLORREF kOutlineColor = RGB(8, 8, 8);
constexpr COLORREF kBackdropColor = RGB(16, 16, 16);

constexpr std::array<std::pair<int, int>, 8> kOutlineOffsets{{
    { -1, -1 }, { 0, -1 }, { 1, -1 },
    { -1,  0 },            { 1,  0 },
    { -1,  1 }, { 0,  1 }, { 1,  1 }
}};

COLORREF ToColorRef(OverlayColor color)
{
    return static_cast<COLORREF>(color);
}

RECT ToRect(const OverlayRect& rect)
{
    return RECT{rect.left, rect.top, rect.right, rect.bottom};
}

class WindowsOverlayBackend final : public OverlayBackend
{
public:
    bool Init(const char* title, int width, int height) override;
    void Shutdown() override;

    bool IsRunning() const override;
    void PumpEvents() override;
    void Render(const OverlayState& state) override;

    void SetVisible(bool visible) override;
    void SetClickThrough(bool enabled) override;
    void SetAlwaysOnTop(bool enabled) override;
    void BringToTop() override;

private:
    HFONT FontForSize(int size);
    void ReleaseFonts();
    void ApplyClickThrough(bool enabled);
    RECT ContentBounds(const OverlayState& state) const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    WNDCLASSW windowClass_ = {};
    std::map<int, HFONT> fonts_;
    OverlayState state_;

    int virtualX_ = 0;
    int virtualY_ = 0;
    int virtualW_ = 0;
    int virtualH_ = 0;

    RECT paintedBounds_{};

    bool running_ = false;
};

bool WindowsOverlayBackend::Init(const char*, int, int)
{
    windowClass_ = {};
    windowClass_.lpfnWndProc = WndProc;
    windowClass_.hInstance = GetModuleHandleW(nullptr);
    windowClass_.lpszClassName = L"RuneHelperOverlay";

    RegisterClassW(&windowClass_);

    virtualX_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
    virtualY_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
    virtualW_ = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virtualH_ = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        windowClass_.lpszClassName,
        L"RuneHelperOverlay",
        WS_POPUP,
        virtualX_,
        virtualY_,
        virtualW_,
        virtualH_,
        nullptr,
        nullptr,
        windowClass_.hInstance,
        this
    );

    if (!hwnd_)
    {
        LOG_ERROR("Windows overlay: CreateWindowEx failed");
        return false;
    }

    SetLayeredWindowAttributes(hwnd_, kTransparentKey, 0, LWA_COLORKEY);
    if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE))
        LOG_ERROR("Windows overlay: SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed");


    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    running_ = true;
    LOG_INFO("Windows overlay backend initialized");
    return true;
}

void WindowsOverlayBackend::Shutdown()
{
    running_ = false;

    ReleaseFonts();

    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    if (windowClass_.lpszClassName)
    {
        UnregisterClassW(windowClass_.lpszClassName, windowClass_.hInstance);
        windowClass_ = {};
    }
}

bool WindowsOverlayBackend::IsRunning() const
{
    return running_;
}

void WindowsOverlayBackend::PumpEvents()
{
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

RECT WindowsOverlayBackend::ContentBounds(const OverlayState& state) const
{
    RECT bounds{};

    const int lineHeight = std::max(16, state.fontSize + 8);

    for (const auto& text : state.texts)
    {
        const int x = text.x - virtualX_;
        const int y = text.y - virtualY_;
        const int size = text.fontSize > 0 ? text.fontSize : state.fontSize;
        const int width = static_cast<int>(ToWide(text.text).size()) * std::max(8, size) + 32;
        const int half = std::max(lineHeight, size + 8);

        const RECT box{ x - 8, y - half, x + width, y + half };

        RECT merged{};
        UnionRect(&merged, &bounds, &box);
        bounds = merged;
    }

    for (const OverlayMark& mark : state.marks)
    {
        const RECT box{
            mark.x - virtualX_ - 4,
            mark.y - virtualY_ - 4,
            mark.x - virtualX_ + mark.width + 4,
            mark.y - virtualY_ + mark.height + 4
        };

        RECT merged{};
        UnionRect(&merged, &bounds, &box);
        bounds = merged;
    }

    if (state.previewEnabled)
    {
        const RECT preview{
            state.previewRect.left - virtualX_ - 4,
            state.previewRect.top - virtualY_ - 4,
            state.previewRect.right - virtualX_ + 4,
            state.previewRect.bottom - virtualY_ + 4
        };

        RECT merged{};
        UnionRect(&merged, &bounds, &preview);
        bounds = merged;
    }

    return bounds;
}

void WindowsOverlayBackend::Render(const OverlayState& state)
{
    if (!hwnd_ || !running_)
        return;

    const bool fontChanged = state_.fontSize != state.fontSize;

    if (fontChanged)
        ReleaseFonts();

    const RECT newBounds = ContentBounds(state);

    RECT dirty{};
    UnionRect(&dirty, &paintedBounds_, &newBounds);

    state_ = state;
    paintedBounds_ = newBounds;

    if (fontChanged || IsRectEmpty(&dirty))
        InvalidateRect(hwnd_, nullptr, FALSE);
    else
        InvalidateRect(hwnd_, &dirty, FALSE);
}

void WindowsOverlayBackend::SetVisible(bool visible)
{
    if (hwnd_)
        ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
}

void WindowsOverlayBackend::SetClickThrough(bool enabled)
{
    ApplyClickThrough(enabled);
}

void WindowsOverlayBackend::SetAlwaysOnTop(bool enabled)
{
    if (!hwnd_)
        return;

    SetWindowPos(
        hwnd_,
        enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}

void WindowsOverlayBackend::BringToTop()
{
    SetAlwaysOnTop(true);
}

HFONT WindowsOverlayBackend::FontForSize(int size)
{
    const int clamped = std::clamp(size, 6, 96);

    const auto it = fonts_.find(clamped);

    if (it != fonts_.end())
        return it->second;

    HDC hdc = GetDC(nullptr);
    const int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);

    const int height = -MulDiv(clamped, dpi, 72);
    HFONT font = CreateFontW(
        height,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );

    fonts_[clamped] = font;

    return font;
}

void WindowsOverlayBackend::ReleaseFonts()
{
    for (auto& [size, font] : fonts_)
    {
        if (font)
            DeleteObject(font);
    }

    fonts_.clear();
}

void WindowsOverlayBackend::ApplyClickThrough(bool enabled)
{
    if (!hwnd_)
        return;

    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    style |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    if (enabled)
        style |= WS_EX_TRANSPARENT;
    else
        style &= ~WS_EX_TRANSPARENT;

    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
}

LRESULT CALLBACK WindowsOverlayBackend::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WindowsOverlayBackend* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<WindowsOverlayBackend*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    else
    {
        self = reinterpret_cast<WindowsOverlayBackend*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client;
        GetClientRect(hwnd, &client);

        HBRUSH bg = CreateSolidBrush(kTransparentKey);
        FillRect(hdc, &client, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);

        if (self->state_.previewEnabled)
        {
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(HOLLOW_BRUSH)));
            RECT rect = ToRect(self->state_.previewRect);

            Rectangle(hdc, 
                rect.left   - self->virtualX_, 
                rect.top    - self->virtualY_, 
                rect.right  - self->virtualX_, 
                rect.bottom - self->virtualY_
            );

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        for (const OverlayMark& mark : self->state_.marks)
        {
            HPEN pen = CreatePen(PS_SOLID, 2, ToColorRef(mark.color));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

            const int left = mark.x - self->virtualX_;
            const int top = mark.y - self->virtualY_;

            Rectangle(hdc, left, top, left + mark.width, top + mark.height);

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        HGDIOBJ oldFont = nullptr;

        for (const auto& text : self->state_.texts)
        {
            const int size = text.fontSize > 0 ? text.fontSize : self->state_.fontSize;
            const std::wstring wide = ToWide(text.text);

            if (HFONT font = self->FontForSize(size))
            {
                HGDIOBJ previous = SelectObject(hdc, font);

                if (!oldFont)
                    oldFont = previous;
            }

            const int x = text.x - self->virtualX_;
            const int y = text.y - self->virtualY_;
            RECT rect{ x, y - size, x + 600, y + size };

            if (self->state_.background)
            {
                SIZE extent{};
                GetTextExtentPoint32W(hdc, wide.c_str(), static_cast<int>(wide.size()), &extent);

                const RECT backdrop{
                    x - 4,
                    y - extent.cy / 2 - 2,
                    x + extent.cx + 4,
                    y + extent.cy / 2 + 2
                };

                HBRUSH shade = CreateSolidBrush(kBackdropColor);
                FillRect(hdc, &backdrop, shade);
                DeleteObject(shade);
            }

            if (self->state_.outline)
            {
                SetTextColor(hdc, kOutlineColor);

                for (const auto& [dx, dy] : kOutlineOffsets)
                {
                    RECT shifted{ rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy };
                    DrawTextW(hdc, wide.c_str(), -1, &shifted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
                }
            }

            SetTextColor(hdc, ToColorRef(text.color));
            DrawTextW(hdc, wide.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }

        if (oldFont)
            SelectObject(hdc, oldFont);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        self->running_ = false;
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
}

std::unique_ptr<OverlayBackend> CreateOverlayBackend()
{
    return std::make_unique<WindowsOverlayBackend>();
}
