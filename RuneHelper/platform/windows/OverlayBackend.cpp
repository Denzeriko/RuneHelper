#include "platform/OverlayBackend.h"

#include <windows.h>

#include "core/Logger.h"
#include "ui/OverlayState.h"

namespace
{
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
    void RecreateFont(int size);
    void ApplyClickThrough(bool enabled);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    WNDCLASSW windowClass_ = {};
    HFONT font_ = nullptr;
    OverlayState state_;
    bool running_ = false;
};

bool WindowsOverlayBackend::Init(const char*, int, int)
{
    windowClass_ = {};
    windowClass_.lpfnWndProc = WndProc;
    windowClass_.hInstance = GetModuleHandleW(nullptr);
    windowClass_.lpszClassName = L"RuneHelperOverlay";

    RegisterClassW(&windowClass_);

    state_.virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    state_.virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    state_.virtualW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    state_.virtualH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        windowClass_.lpszClassName,
        L"RuneHelperOverlay",
        WS_POPUP,
        state_.virtualX,
        state_.virtualY,
        state_.virtualW,
        state_.virtualH,
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

    SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY);
    RecreateFont(state_.fontSize);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    running_ = true;
    LOG_INFO("Windows overlay backend initialized");
    return true;
}

void WindowsOverlayBackend::Shutdown()
{
    running_ = false;

    if (font_)
    {
        DeleteObject(font_);
        font_ = nullptr;
    }

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

void WindowsOverlayBackend::Render(const OverlayState& state)
{
    if (!hwnd_ || !running_)
        return;

    if (state_.fontSize != state.fontSize)
        RecreateFont(state.fontSize);

    state_ = state;
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateWindow(hwnd_);
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

void WindowsOverlayBackend::RecreateFont(int size)
{
    if (size <= 0)
        return;

    if (font_)
    {
        DeleteObject(font_);
        font_ = nullptr;
    }

    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);

    int height = -MulDiv(size, dpi, 72);

    font_ = CreateFontW(
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

        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &client, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);

        if (self->state_.previewEnabled)
        {
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(HOLLOW_BRUSH)));
            RECT rect = ToRect(self->state_.previewRect);

            Rectangle(hdc, rect.left - self->state_.virtualX, rect.top - self->state_.virtualY, rect.right - self->state_.virtualX, rect.bottom - self->state_.virtualY);

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        HFONT oldFont = nullptr;

        if (self->font_)
            oldFont = static_cast<HFONT>(SelectObject(hdc, self->font_));

        for (const auto& text : self->state_.texts)
        {
            SetTextColor(hdc, ToColorRef(text.color));
            int x = text.x - self->state_.virtualX;
            int y = text.y - self->state_.virtualY;
            RECT rect{x, y - 20, x + 300, y + 20};
            DrawTextW(hdc, text.text.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
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
