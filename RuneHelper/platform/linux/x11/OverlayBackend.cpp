#include "platform/OverlayBackend.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "core/Logger.h"
#include "ui/OverlayState.h"

namespace
{
bool IsWaylandSession()
{
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");
    return sessionType && std::string(sessionType) == "wayland";
}

std::string ToNarrow(const std::wstring& text)
{
    std::string result;
    result.reserve(text.size());

    for (wchar_t ch : text)
        result.push_back(ch >= 0 && ch <= 127 ? static_cast<char>(ch) : '?');

    return result;
}

unsigned long XColorFromOverlayColor(Display* display, OverlayColor color)
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

class LinuxOverlayBackend final : public OverlayBackend
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
    void ResizeAndMove();
    void Redraw();
    void SetOpacity(unsigned long opacity);

    Display* display_ = nullptr;
    Window window_ = 0;
    GC gc_ = nullptr;
    OverlayState state_;

    bool running_ = false;
    bool visible_ = false;
    bool clickThroughLogged_ = false;
    int windowX_ = 20;
    int windowY_ = 20;
    int windowW_ = 360;
    int windowH_ = 120;
};

bool LinuxOverlayBackend::Init(const char* title, int width, int height)
{
    windowW_ = std::max(1, width);
    windowH_ = std::max(1, height);

    if (IsWaylandSession())
    {
        LOG_ERROR("Linux overlay requires an X11 session; Wayland is not supported yet");
        return true;
    }

    display_ = XOpenDisplay(nullptr);

    if (!display_)
    {
        LOG_ERROR("Linux overlay requires X11, but XOpenDisplay failed");
        return true;
    }

    int screen = DefaultScreen(display_);
    Window root = RootWindow(display_, screen);

    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(display_, screen);
    attrs.border_pixel = BlackPixel(display_, screen);

    window_ = XCreateWindow(
        display_,
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

    if (!window_)
    {
        LOG_ERROR("Linux overlay failed to create X11 window");
        XCloseDisplay(display_);
        display_ = nullptr;
        return true;
    }

    XStoreName(display_, window_, title ? title : "RuneHelper Overlay");
    SetOpacity(0xd0000000UL);
    SetAlwaysOnTop(true);

    XSelectInput(display_, window_, ExposureMask | StructureNotifyMask);

    gc_ = XCreateGC(display_, window_, 0, nullptr);
    XSetForeground(display_, gc_, WhitePixel(display_, screen));

    XMapRaised(display_, window_);
    XFlush(display_);

    running_ = true;
    visible_ = true;
    LOG_INFO("Linux X11 overlay backend initialized");
    return true;
}

void LinuxOverlayBackend::Shutdown()
{
    running_ = false;
    visible_ = false;

    if (display_ && gc_)
    {
        XFreeGC(display_, gc_);
        gc_ = nullptr;
    }

    if (display_ && window_)
    {
        XDestroyWindow(display_, window_);
        window_ = 0;
    }

    if (display_)
    {
        XCloseDisplay(display_);
        display_ = nullptr;
    }
}

bool LinuxOverlayBackend::IsRunning() const
{
    return running_;
}

void LinuxOverlayBackend::PumpEvents()
{
    if (!display_ || !window_ || !running_)
        return;

    while (XPending(display_) > 0)
    {
        XEvent event{};
        XNextEvent(display_, &event);

        if (event.type == Expose)
            Redraw();
    }
}

void LinuxOverlayBackend::Render(const OverlayState& state)
{
    if (!display_ || !window_ || !running_)
        return;

    state_ = state;
    ResizeAndMove();
    Redraw();
}

void LinuxOverlayBackend::SetVisible(bool visible)
{
    visible_ = visible;

    if (!display_ || !window_)
        return;

    if (visible_)
        XMapRaised(display_, window_);
    else
        XUnmapWindow(display_, window_);

    XFlush(display_);
}

void LinuxOverlayBackend::SetClickThrough(bool enabled)
{
    if (enabled && !clickThroughLogged_)
    {
        LOG_INFO("Linux overlay: click-through is not implemented for the X11 backend yet");
        clickThroughLogged_ = true;
    }
}

void LinuxOverlayBackend::SetAlwaysOnTop(bool enabled)
{
    if (!display_ || !window_)
        return;

    Atom stateAtom = XInternAtom(display_, "_NET_WM_STATE", False);
    Atom aboveAtom = XInternAtom(display_, "_NET_WM_STATE_ABOVE", False);

    if (enabled)
    {
        XChangeProperty(
            display_,
            window_,
            stateAtom,
            XA_ATOM,
            32,
            PropModeReplace,
            reinterpret_cast<unsigned char*>(&aboveAtom),
            1
        );
    }
    else
    {
        XDeleteProperty(display_, window_, stateAtom);
    }

    XFlush(display_);
}

void LinuxOverlayBackend::BringToTop()
{
    if (!display_ || !window_)
        return;

    XRaiseWindow(display_, window_);
    XFlush(display_);
}

void LinuxOverlayBackend::ResizeAndMove()
{
    if (!display_ || !window_)
        return;

    if (state_.texts.empty())
    {
        if (state_.previewEnabled)
        {
            windowX_ = std::max(0L, state_.previewRect.left);
            windowY_ = std::max(0L, state_.previewRect.top);
            windowW_ = std::max(1L, state_.previewRect.right - state_.previewRect.left);
            windowH_ = std::max(1L, state_.previewRect.bottom - state_.previewRect.top);
        }
        else
        {
            windowW_ = 1;
            windowH_ = 1;
        }
    }
    else
    {
        int maxLen = 1;

        for (const auto& text : state_.texts)
            maxLen = std::max(maxLen, static_cast<int>(text.text.size()));

        windowX_ = std::max(0, state_.texts.front().x);
        windowY_ = std::max(0, state_.texts.front().y);
        windowW_ = std::clamp(maxLen * std::max(8, state_.fontSize / 2) + 28, 320, 900);
        windowH_ = std::max(80, static_cast<int>(state_.texts.size()) * (state_.fontSize + 8) + 20);
    }

    XMoveResizeWindow(
        display_,
        window_,
        windowX_,
        windowY_,
        static_cast<unsigned int>(windowW_),
        static_cast<unsigned int>(windowH_)
    );
}

void LinuxOverlayBackend::Redraw()
{
    if (!display_ || !window_ || !gc_)
        return;

    int screen = DefaultScreen(display_);

    XSetForeground(display_, gc_, BlackPixel(display_, screen));
    XFillRectangle(
        display_,
        window_,
        gc_,
        0,
        0,
        static_cast<unsigned int>(windowW_),
        static_cast<unsigned int>(windowH_)
    );

    XFontStruct* font = XLoadQueryFont(display_, "fixed");

    if (font)
        XSetFont(display_, gc_, font->fid);

    if (state_.previewEnabled)
    {
        XSetForeground(display_, gc_, XColorFromOverlayColor(display_, OverlayRgb(0, 255, 0)));
        XDrawRectangle(
            display_,
            window_,
            gc_,
            0,
            0,
            static_cast<unsigned int>(std::max(1, windowW_ - 1)),
            static_cast<unsigned int>(std::max(1, windowH_ - 1))
        );
    }

    const int lineHeight = std::max(14, state_.fontSize + 8);
    int y = 18;

    for (const auto& text : state_.texts)
    {
        std::string narrow = ToNarrow(text.text);
        XSetForeground(display_, gc_, XColorFromOverlayColor(display_, text.color));
        XDrawString(display_, window_, gc_, 12, y, narrow.c_str(), static_cast<int>(narrow.size()));
        y += lineHeight;
    }

    if (font)
        XFreeFont(display_, font);

    XFlush(display_);
}

void LinuxOverlayBackend::SetOpacity(unsigned long opacity)
{
    if (!display_ || !window_)
        return;

    Atom opacityAtom = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(
        display_,
        window_,
        opacityAtom,
        XA_CARDINAL,
        32,
        PropModeReplace,
        reinterpret_cast<unsigned char*>(&opacity),
        1
    );
}
}

std::unique_ptr<OverlayBackend> CreateOverlayBackend()
{
    return std::make_unique<LinuxOverlayBackend>();
}
