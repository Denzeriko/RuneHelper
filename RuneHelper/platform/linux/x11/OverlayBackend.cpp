#include "platform/OverlayBackend.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include "core/Logger.h"
#include "ui/OverlayRenderer.h"
#include "ui/OverlayState.h"

namespace
{
constexpr int kCoverageThreshold = 96;

bool IsWaylandSession()
{
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");
    return sessionType && std::string(sessionType) == "wayland";
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

    bool EnsureSurfaces(int width, int height);
    void ReleaseSurfaces();
    void FillColorImage(const cv::Mat& canvas);
    void FillMaskImage(const cv::Mat& alpha);

    Display* display_ = nullptr;
    Window window_ = 0;
    GC gc_ = nullptr;
    OverlayState state_;

    cv::Mat canvas_;
    cv::Mat alpha_;

    XImage* colorImage_ = nullptr;
    XImage* maskImage_ = nullptr;
    Pixmap maskPixmap_ = 0;
    GC maskGc_ = nullptr;
    int surfaceW_ = 0;
    int surfaceH_ = 0;
    bool packedBgrx_ = false;

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
        return false;
    }

    display_ = XOpenDisplay(nullptr);

    if (!display_)
    {
        LOG_ERROR("Linux overlay requires X11, but XOpenDisplay failed");
        return false;
    }

    const int screen = DefaultScreen(display_);
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
        return false;
    }

    XStoreName(display_, window_, title ? title : "RuneHelper Overlay");
    SetOpacity(0xd0000000UL);
    SetAlwaysOnTop(true);

    XSelectInput(display_, window_, ExposureMask | StructureNotifyMask);

    gc_ = XCreateGC(display_, window_, 0, nullptr);

    Visual* visual = DefaultVisual(display_, screen);

    packedBgrx_ = ImageByteOrder(display_) == LSBFirst && visual->red_mask == 0x00ff0000UL && visual->green_mask == 0x0000ff00UL &&
                  visual->blue_mask == 0x000000ffUL;

    if (!packedBgrx_)
        LOG_INFO("Linux overlay: the X visual is not packed BGRX, falling back to per-pixel upload");

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

    if (display_)
        ReleaseSurfaces();

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
        XChangeProperty(display_, window_, stateAtom, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&aboveAtom), 1);
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

    Window root = 0;
    Window parent = 0;
    Window* children = nullptr;
    unsigned int count = 0;

    if (XQueryTree(display_, DefaultRootWindow(display_), &root, &parent, &children, &count))
    {
        const bool alreadyOnTop = count > 0 && children[count - 1] == window_;

        if (children)
            XFree(children);

        if (alreadyOnTop)
            return;
    }

    XRaiseWindow(display_, window_);
    XFlush(display_);
}

void LinuxOverlayBackend::ReleaseSurfaces()
{
    if (colorImage_)
    {
        XDestroyImage(colorImage_);
        colorImage_ = nullptr;
    }

    if (maskImage_)
    {
        XDestroyImage(maskImage_);
        maskImage_ = nullptr;
    }

    if (maskGc_)
    {
        XFreeGC(display_, maskGc_);
        maskGc_ = nullptr;
    }

    if (maskPixmap_)
    {
        XFreePixmap(display_, maskPixmap_);
        maskPixmap_ = 0;
    }

    surfaceW_ = 0;
    surfaceH_ = 0;
}

bool LinuxOverlayBackend::EnsureSurfaces(int width, int height)
{
    if (colorImage_ && maskImage_ && maskPixmap_ && surfaceW_ == width && surfaceH_ == height)
        return true;

    ReleaseSurfaces();

    const int screen = DefaultScreen(display_);
    Visual* visual = DefaultVisual(display_, screen);
    const unsigned int depth = static_cast<unsigned int>(DefaultDepth(display_, screen));

    colorImage_ = XCreateImage(
        display_,
        visual,
        depth,
        ZPixmap,
        0,
        nullptr,
        static_cast<unsigned int>(width),
        static_cast<unsigned int>(height),
        32,
        0
    );

    maskImage_ = XCreateImage(
        display_,
        visual,
        1,
        XYBitmap,
        0,
        nullptr,
        static_cast<unsigned int>(width),
        static_cast<unsigned int>(height),
        8,
        0
    );

    if (!colorImage_ || !maskImage_)
    {
        LOG_ERROR("Linux overlay: XCreateImage failed");
        ReleaseSurfaces();
        return false;
    }

    colorImage_->data =
        static_cast<char*>(std::calloc(static_cast<std::size_t>(colorImage_->bytes_per_line) * static_cast<std::size_t>(height), 1));

    maskImage_->data =
        static_cast<char*>(std::calloc(static_cast<std::size_t>(maskImage_->bytes_per_line) * static_cast<std::size_t>(height), 1));

    if (!colorImage_->data || !maskImage_->data)
    {
        LOG_ERROR("Linux overlay: could not allocate the overlay images");
        ReleaseSurfaces();
        return false;
    }

    maskPixmap_ = XCreatePixmap(display_, window_, static_cast<unsigned int>(width), static_cast<unsigned int>(height), 1);

    if (!maskPixmap_)
    {
        LOG_ERROR("Linux overlay: XCreatePixmap failed for the shape mask");
        ReleaseSurfaces();
        return false;
    }

    maskGc_ = XCreateGC(display_, maskPixmap_, 0, nullptr);

    if (!maskGc_)
    {
        LOG_ERROR("Linux overlay: XCreateGC failed for the shape mask");
        ReleaseSurfaces();
        return false;
    }

    XSetForeground(display_, maskGc_, 1);
    XSetBackground(display_, maskGc_, 0);

    surfaceW_ = width;
    surfaceH_ = height;

    return true;
}

void LinuxOverlayBackend::FillColorImage(const cv::Mat& canvas)
{
    const std::size_t rowBytes = static_cast<std::size_t>(canvas.cols) * 4;

    if (packedBgrx_ && colorImage_->bits_per_pixel == 32)
    {
        for (int y = 0; y < canvas.rows; ++y)
        {
            std::memcpy(
                colorImage_->data + static_cast<std::size_t>(y) * static_cast<std::size_t>(colorImage_->bytes_per_line),
                canvas.ptr<unsigned char>(y),
                rowBytes
            );
        }

        return;
    }

    for (int y = 0; y < canvas.rows; ++y)
    {
        const unsigned char* row = canvas.ptr<unsigned char>(y);

        for (int x = 0; x < canvas.cols; ++x)
        {
            const unsigned char* pixel = row + static_cast<std::size_t>(x) * 4;

            XPutPixel(
                colorImage_,
                x,
                y,
                (static_cast<unsigned long>(pixel[2]) << 16) | (static_cast<unsigned long>(pixel[1]) << 8) |
                    static_cast<unsigned long>(pixel[0])
            );
        }
    }
}

void LinuxOverlayBackend::FillMaskImage(const cv::Mat& alpha)
{
    const bool lsbFirst = maskImage_->bitmap_bit_order == LSBFirst;
    const std::size_t stride = static_cast<std::size_t>(maskImage_->bytes_per_line);

    std::memset(maskImage_->data, 0, stride * static_cast<std::size_t>(alpha.rows));

    for (int y = 0; y < alpha.rows; ++y)
    {
        const unsigned char* row = alpha.ptr<unsigned char>(y);
        unsigned char* out = reinterpret_cast<unsigned char*>(maskImage_->data) + static_cast<std::size_t>(y) * stride;

        for (int x = 0; x < alpha.cols; ++x)
        {
            if (!row[x])
                continue;

            const int bit = lsbFirst ? (x & 7) : (7 - (x & 7));
            out[x >> 3] |= static_cast<unsigned char>(1u << bit);
        }
    }
}

void LinuxOverlayBackend::ResizeAndMove()
{
    if (!display_ || !window_)
        return;

    const cv::Rect content = OverlayRenderer::ContentBounds(state_);

    if (content.empty())
    {
        windowW_ = 1;
        windowH_ = 1;
    }
    else
    {
        windowX_ = std::max(0, content.x);
        windowY_ = std::max(0, content.y);
        windowW_ = std::max(1, content.x + content.width - windowX_);
        windowH_ = std::max(1, content.y + content.height - windowY_);
    }

    XMoveResizeWindow(display_, window_, windowX_, windowY_, static_cast<unsigned int>(windowW_), static_cast<unsigned int>(windowH_));
}

void LinuxOverlayBackend::Redraw()
{
    if (!display_ || !window_ || !gc_)
        return;

    if (!EnsureSurfaces(windowW_, windowH_))
        return;

    if (canvas_.rows != windowH_ || canvas_.cols != windowW_)
        canvas_.create(windowH_, windowW_, CV_8UC4);

    canvas_.setTo(cv::Scalar(0, 0, 0, 0));

    OverlayRenderer::Paint(canvas_, cv::Point(windowX_, windowY_), state_);

    cv::extractChannel(canvas_, alpha_, 3);
    cv::threshold(alpha_, alpha_, kCoverageThreshold - 1, 255, cv::THRESH_BINARY);

    FillMaskImage(alpha_);

    XPutImage(
        display_,
        maskPixmap_,
        maskGc_,
        maskImage_,
        0,
        0,
        0,
        0,
        static_cast<unsigned int>(windowW_),
        static_cast<unsigned int>(windowH_)
    );

    XShapeCombineMask(display_, window_, ShapeBounding, 0, 0, maskPixmap_, ShapeSet);

    FillColorImage(canvas_);

    XPutImage(
        display_,
        window_,
        gc_,
        colorImage_,
        0,
        0,
        0,
        0,
        static_cast<unsigned int>(windowW_),
        static_cast<unsigned int>(windowH_)
    );

    XFlush(display_);
}

void LinuxOverlayBackend::SetOpacity(unsigned long opacity)
{
    if (!display_ || !window_)
        return;

    Atom opacityAtom = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(display_, window_, opacityAtom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&opacity), 1);
}
}

std::unique_ptr<OverlayBackend> CreateOverlayBackend()
{
    return std::make_unique<LinuxOverlayBackend>();
}
