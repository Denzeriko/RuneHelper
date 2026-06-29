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
    bool sameRect =
        previewRect_.left == rect.left &&
        previewRect_.top == rect.top &&
        previewRect_.right == rect.right &&
        previewRect_.bottom == rect.bottom;

    if (previewEnabled_ == enabled && sameRect)
        return;

    previewEnabled_ = enabled;
    previewRect_ = rect;

    InvalidateRect(hwnd_, nullptr, FALSE);
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

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include "core/Logger.h"

namespace
{
Display* AsDisplay(void* display)
{
    return static_cast<Display*>(display);
}

GC AsGc(void* gc)
{
    return static_cast<GC>(gc);
}

std::string ToNarrow(const std::wstring& text)
{
    std::string result;
    result.reserve(text.size());

    for (wchar_t ch : text)
        result.push_back(ch >= 0 && ch <= 127 ? static_cast<char>(ch) : '?');

    return result;
}

unsigned long XColorFromColorRef(Display* display, COLORREF color)
{
    int screen = DefaultScreen(display);
    int r = static_cast<int>(color & 0xff);
    int g = static_cast<int>((color >> 8) & 0xff);
    int b = static_cast<int>((color >> 16) & 0xff);

    return (static_cast<unsigned long>(r) << 16) |
           (static_cast<unsigned long>(g) << 8) |
           static_cast<unsigned long>(b) |
           (BlackPixel(display, screen) & 0xff000000UL);
}

bool IsWaylandSession()
{
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");
    return sessionType && std::string(sessionType) == "wayland";
}
}

bool OverlayWindow::Create()
{
    if (IsWaylandSession())
    {
        LOG_ERROR("Linux overlay requires X11; Wayland is not supported yet");
        return true;
    }

    Display* display = XOpenDisplay(nullptr);

    if (!display)
    {
        LOG_ERROR("Linux overlay requires X11, but XOpenDisplay failed");
        return true;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);

    Window window = XCreateWindow(
        display,
        root,
        windowX_,
        windowY_,
        static_cast<unsigned int>(windowW_),
        static_cast<unsigned int>(windowH_),
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel,
        &attrs
    );

    if (!window)
    {
        LOG_ERROR("Linux overlay failed to create X11 window");
        XCloseDisplay(display);
        return true;
    }

    XStoreName(display, window, "RuneHelper Debug Overlay");

    Atom opacityAtom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
    unsigned long opacity = 0xd0000000UL;
    XChangeProperty(display, window, opacityAtom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&opacity), 1);

    Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom aboveAtom = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, stateAtom, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&aboveAtom), 1);

    XSelectInput(display, window, ExposureMask | StructureNotifyMask);
    XMapRaised(display, window);

    GC gc = XCreateGC(display, window, 0, nullptr);
    XSetForeground(display, gc, WhitePixel(display, screen));

    display_ = display;
    window_ = window;
    gc_ = gc;

    LOG_INFO("Linux debug overlay window created");
    return true;
}

void OverlayWindow::BringToTop()
{
    Display* display = AsDisplay(display_);

    if (!display || !window_)
        return;

    XRaiseWindow(display, static_cast<Window>(window_));
    XFlush(display);
}

void OverlayWindow::SetRegionPreview(bool enabled, const RECT& rect)
{
    previewEnabled_ = enabled;
    previewRect_ = rect;
}

void OverlayWindow::SetTexts(std::vector<OverlayText> texts)
{
    texts_ = std::move(texts);
    ResizeAndMove();
    Redraw();
}

void OverlayWindow::SetFontSize(int size)
{
    if (size <= 0)
        return;

    fontSize_ = size;
    ResizeAndMove();
    Redraw();
}

void OverlayWindow::SetFontSizeForce(int size)
{
    if (size <= 0)
        return;

    fontSize_ = size;
    ResizeAndMove();
    Redraw();
}

void OverlayWindow::PumpMessages()
{
    Display* display = AsDisplay(display_);

    if (!display || !window_)
        return;

    while (XPending(display) > 0)
    {
        XEvent event{};
        XNextEvent(display, &event);

        if (event.type == Expose)
            Redraw();
    }
}

void OverlayWindow::ResizeAndMove()
{
    Display* display = AsDisplay(display_);

    if (!display || !window_ || texts_.empty())
        return;

    int maxLen = 1;

    for (const auto& text : texts_)
        maxLen = std::max(maxLen, static_cast<int>(text.text.size()));

    windowX_ = std::max(0, texts_.front().x);
    windowY_ = std::max(0, texts_.front().y);
    windowW_ = std::clamp(maxLen * std::max(8, fontSize_ / 2) + 28, 320, 900);
    windowH_ = std::max(80, static_cast<int>(texts_.size()) * (fontSize_ + 8) + 20);

    XMoveResizeWindow(
        display,
        static_cast<Window>(window_),
        windowX_,
        windowY_,
        static_cast<unsigned int>(windowW_),
        static_cast<unsigned int>(windowH_)
    );
}

void OverlayWindow::Redraw()
{
    Display* display = AsDisplay(display_);
    GC gc = AsGc(gc_);

    if (!display || !window_ || !gc)
        return;

    int screen = DefaultScreen(display);
    Window window = static_cast<Window>(window_);

    XSetForeground(display, gc, BlackPixel(display, screen));
    XFillRectangle(display, window, gc, 0, 0, static_cast<unsigned int>(windowW_), static_cast<unsigned int>(windowH_));

    XFontStruct* font = XLoadQueryFont(display, "fixed");

    if (font)
        XSetFont(display, gc, font->fid);

    const int lineHeight = std::max(14, fontSize_ + 8);
    int y = 18;

    for (const auto& text : texts_)
    {
        std::string narrow = ToNarrow(text.text);
        XSetForeground(display, gc, XColorFromColorRef(display, text.color));
        XDrawString(display, window, gc, 12, y, narrow.c_str(), static_cast<int>(narrow.size()));
        y += lineHeight;
    }

    if (font)
        XFreeFont(display, font);

    XFlush(display);
}

#endif
