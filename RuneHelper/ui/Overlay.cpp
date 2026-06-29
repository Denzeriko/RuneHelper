#include "Overlay.h"

#ifdef _WIN32

#include <windows.h>
#include <vector>

#include "core/Logger.h"

bool OverlayWindow::Create()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RuneHelperOverlay";

    RegisterClassW(&wc);

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST |
        WS_EX_LAYERED |
        WS_EX_TRANSPARENT |
        WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"RuneHelperOverlay",
        WS_POPUP,
        x, y, w, h,
        nullptr,
        nullptr,
        wc.hInstance,
        this
    );

    if (!hwnd_) {
        LOG_ERROR("OverlayWindow::Create() No HWND!");
        return false;
    }

    SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    LOG_INFO("OverlayWindow::Create() TRUE");

    return true;
}

void OverlayWindow::BringToTop()
{
    if (!hwnd_)
        return;

    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE |
        SWP_NOSIZE |
        SWP_NOACTIVATE |
        SWP_SHOWWINDOW
    );
}

void OverlayWindow::SetRegionPreview(bool enabled, const RECT& rect)
{
    previewEnabled_ = enabled;
    previewRect_ = rect;

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void OverlayWindow::SetTexts(std::vector<OverlayText> texts)
{
    texts_ = std::move(texts);
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateWindow(hwnd_);
}

void OverlayWindow::SetFontSize(int size)
{
    if (size <= 0)
        return;

    if (fontSize_ == size)
        return;

    LOG_INFO("OverlayWindow::SetFontSize -> call");

    fontSize_ = size;

    RecreateFont();

    LOG_INFO("RecreateFont() done");

    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, TRUE);
}

void OverlayWindow::SetFontSizeForce(int size)
{
    if (size <= 0)
        return;

    LOG_INFO("OverlayWindow::SetFontSizeForce -> call");

    fontSize_ = size;

    RecreateFont();

    LOG_INFO("RecreateFont() done");

    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, TRUE);
}

void OverlayWindow::PumpMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void OverlayWindow::RecreateFont()
{
    if (font_)
    {
        DeleteObject(font_);
        font_ = nullptr;
    }

    HDC hdc = GetDC(nullptr);

    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);

    ReleaseDC(nullptr, hdc);

    int height = -MulDiv(fontSize_, dpi, 72);

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

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    OverlayWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);

        self = reinterpret_cast<OverlayWindow*>(cs->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        self->hwnd_ = hwnd;
    }
    else
    {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
        SetTextColor(hdc, RGB(80, 240, 80));

        if (self->previewEnabled_)
        {
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
            HGDIOBJ oldPen = SelectObject(hdc, pen);

            HBRUSH oldBrush = (HBRUSH)SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

            Rectangle(
                hdc,
                self->previewRect_.left,
                self->previewRect_.top,
                self->previewRect_.right,
                self->previewRect_.bottom
            );

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        HFONT oldFont = nullptr;

        if (self->font_)
            oldFont = (HFONT)SelectObject(hdc, self->font_);

        for (const auto& t : self->texts_)
        {
            SetTextColor(hdc, t.color);

            RECT r{t.x, t.y - 20, t.x + 300, t.y + 20};

            DrawTextW(hdc, t.text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }

        if (oldFont)
            SelectObject(hdc, oldFont);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
#else

#include <utility>

#include "core/Logger.h"

bool OverlayWindow::Create()
{
    LOG_ERROR("Linux game overlay is not implemented");
    return true;
}

void OverlayWindow::BringToTop()
{
}

void OverlayWindow::SetRegionPreview(bool enabled, const RECT& rect)
{
    previewEnabled_ = enabled;
    previewRect_ = rect;
}

void OverlayWindow::SetTexts(std::vector<OverlayText> texts)
{
    texts_ = std::move(texts);
}

void OverlayWindow::SetFontSize(int size)
{
    fontSize_ = size;
}

void OverlayWindow::SetFontSizeForce(int size)
{
    fontSize_ = size;
}

void OverlayWindow::PumpMessages()
{
}

#endif
