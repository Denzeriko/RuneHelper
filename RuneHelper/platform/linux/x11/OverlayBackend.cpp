#include "platform/OverlayBackend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include "core/Logger.h"
#include "platform/linux/TextRaster.h"
#include "ui/OverlayState.h"

namespace
{
constexpr int kMarkThickness = 2;
constexpr int kTextPadding = 6;
constexpr int kCoverageThreshold = 96;

constexpr std::array<std::pair<int, int>, 8> kOutlineOffsets{{
    { -1, -1 }, { 0, -1 }, { 1, -1 },
    { -1,  0 },            { 1,  0 },
    { -1,  1 }, { 0,  1 }, { 1,  1 }
}};

bool IsWaylandSession()
{
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");
    return sessionType && std::string(sessionType) == "wayland";
}

struct TextPlacement
{
    cv::Rect box;
    cv::Point baseline;
};

struct RenderedText
{
    cv::Rect box;
    XImage* color = nullptr;
    XImage* coverage = nullptr;
};

void DestroyRendered(RenderedText& text)
{
    if (text.color)
        XDestroyImage(text.color);

    if (text.coverage)
        XDestroyImage(text.coverage);

    text.color = nullptr;
    text.coverage = nullptr;
}

cv::Size MeasureText(const std::string& utf8, int pixelHeight, int& descent)
{
    TextRaster& raster = TextRaster::Instance();

    if (raster.Ready())
    {
        descent = raster.Descent(pixelHeight);
        return raster.Measure(utf8, pixelHeight);
    }

    descent = pixelHeight / 3;

    return cv::Size(static_cast<int>(utf8.size()) * std::max(6, pixelHeight / 2 + 2), pixelHeight);
}

TextPlacement PlaceText(const OverlayText& text, int pixelHeight)
{
    int descent = 0;
    const cv::Size size = MeasureText(text.text, pixelHeight, descent);
    const int baselineY = text.y + size.height / 2;

    TextPlacement placement;
    placement.baseline = cv::Point(text.x, baselineY);
    placement.box = cv::Rect(
        text.x - kTextPadding,
        baselineY - size.height - kTextPadding,
        std::max(1, size.width + 2 * kTextPadding),
        std::max(1, size.height + descent + 2 * kTextPadding)
    );

    return placement;
}

cv::Mat RasterizeText(const OverlayText& text, int pixelHeight, const TextPlacement& placement, bool outline)
{
    cv::Mat canvas(placement.box.height, placement.box.width, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    const double r = static_cast<double>(text.color & 0xff);
    const double g = static_cast<double>((text.color >> 8) & 0xff);
    const double b = static_cast<double>((text.color >> 16) & 0xff);

    TextRaster::Instance().Draw(
        canvas,
        text.text,
        placement.baseline - placement.box.tl(),
        pixelHeight,
        cv::Scalar(b, g, r, 255),
        outline
    );

    return canvas;
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
    XFontStruct* FontForSize(int size);
    void ReleaseFonts();
    XImage* CreateColorImage(const cv::Mat& bgra) const;
    XImage* CreateCoverageImage(const cv::Mat& bgra) const;
    void SetOpacity(unsigned long opacity);

    Display* display_ = nullptr;
    Window window_ = 0;
    GC gc_ = nullptr;
    XFontStruct* font_ = nullptr;
    OverlayState state_;

    bool running_ = false;
    bool visible_ = false;
    bool clickThroughLogged_ = false;
    int windowX_ = 20;
    int windowY_ = 20;
    int windowW_ = 360;
    std::map<int, XFontStruct*> fonts_;
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
        return false;
    }

    XStoreName(display_, window_, title ? title : "RuneHelper Overlay");
    SetOpacity(0xd0000000UL);
    SetAlwaysOnTop(true);

    XSelectInput(display_, window_, ExposureMask | StructureNotifyMask);

    gc_ = XCreateGC(display_, window_, 0, nullptr);
    XSetForeground(display_, gc_, WhitePixel(display_, screen));

    font_ = XLoadQueryFont(display_, "fixed");

    if (font_)
        XSetFont(display_, gc_, font_->fid);
    else
        LOG_ERROR("Linux overlay: XLoadQueryFont failed, falling back to the server default font");

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

    if (display_ && font_)
    {
        XFreeFont(display_, font_);
        font_ = nullptr;
    }

    if (display_ && gc_)
    {
        XFreeGC(display_, gc_);
        gc_ = nullptr;
    }

    if (display_)
        ReleaseFonts();

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

XImage* LinuxOverlayBackend::CreateColorImage(const cv::Mat& bgra) const
{
    const int screen = DefaultScreen(display_);

    XImage* image = XCreateImage(
        display_,
        DefaultVisual(display_, screen),
        static_cast<unsigned int>(DefaultDepth(display_, screen)),
        ZPixmap,
        0,
        nullptr,
        static_cast<unsigned int>(bgra.cols),
        static_cast<unsigned int>(bgra.rows),
        32,
        0
    );

    if (!image)
        return nullptr;

    image->data = static_cast<char*>(std::calloc(static_cast<std::size_t>(image->bytes_per_line) * bgra.rows, 1));

    if (!image->data)
    {
        XFree(image);
        return nullptr;
    }

    for (int y = 0; y < bgra.rows; ++y)
    {
        const unsigned char* row = bgra.ptr<unsigned char>(y);

        for (int x = 0; x < bgra.cols; ++x)
        {
            const unsigned char* pixel = row + static_cast<std::size_t>(x) * 4;

            XPutPixel(
                image,
                x,
                y,
                (static_cast<unsigned long>(pixel[2]) << 16) |
                (static_cast<unsigned long>(pixel[1]) << 8) |
                static_cast<unsigned long>(pixel[0])
            );
        }
    }

    return image;
}

XImage* LinuxOverlayBackend::CreateCoverageImage(const cv::Mat& bgra) const
{
    const int screen = DefaultScreen(display_);

    XImage* image = XCreateImage(
        display_,
        DefaultVisual(display_, screen),
        1,
        XYPixmap,
        0,
        nullptr,
        static_cast<unsigned int>(bgra.cols),
        static_cast<unsigned int>(bgra.rows),
        8,
        0
    );

    if (!image)
        return nullptr;

    image->data = static_cast<char*>(std::calloc(static_cast<std::size_t>(image->bytes_per_line) * bgra.rows, 1));

    if (!image->data)
    {
        XFree(image);
        return nullptr;
    }

    for (int y = 0; y < bgra.rows; ++y)
    {
        const unsigned char* row = bgra.ptr<unsigned char>(y);

        for (int x = 0; x < bgra.cols; ++x)
        {
            if (row[static_cast<std::size_t>(x) * 4 + 3] >= kCoverageThreshold)
                XPutPixel(image, x, y, 1);
        }
    }

    return image;
}

XFontStruct* LinuxOverlayBackend::FontForSize(int size)
{
    const int clamped = std::clamp(size, 8, 48);

    const auto it = fonts_.find(clamped);

    if (it != fonts_.end())
        return it->second;

    char pattern[128];
    std::snprintf(pattern, sizeof(pattern), "-*-*-medium-r-normal--%d-*-*-*-*-*-iso8859-1", clamped);

    XFontStruct* font = XLoadQueryFont(display_, pattern);

    if (!font)
        font = XLoadQueryFont(display_, "fixed");

    fonts_[clamped] = font;

    return font;
}

void LinuxOverlayBackend::ReleaseFonts()
{
    for (auto& [size, font] : fonts_)
    {
        if (font)
            XFreeFont(display_, font);
    }

    fonts_.clear();
}

void LinuxOverlayBackend::ResizeAndMove()
{
    if (!display_ || !window_)
        return;

    bool any = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    auto merge = [&](int x, int y, int w, int h)
        {
            if (!any)
            {
                left = x;
                top = y;
                right = x + w;
                bottom = y + h;
                any = true;
                return;
            }

            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + w);
            bottom = std::max(bottom, y + h);
        };

    for (const auto& text : state_.texts)
    {
        const int size = text.fontSize > 0 ? text.fontSize : state_.fontSize;
        const cv::Rect box = PlaceText(text, size).box;
        merge(box.x, box.y, box.width, box.height);
    }

    for (const OverlayMark& mark : state_.marks)
        merge(mark.x, mark.y, mark.width, mark.height);

    if (state_.previewEnabled)
    {
        merge(
            static_cast<int>(state_.previewRect.left),
            static_cast<int>(state_.previewRect.top),
            static_cast<int>(state_.previewRect.right - state_.previewRect.left),
            static_cast<int>(state_.previewRect.bottom - state_.previewRect.top));
    }

    if (!any)
    {
        windowW_ = 1;
        windowH_ = 1;
    }
    else
    {
        windowX_ = std::max(0, left);
        windowY_ = std::max(0, top);
        windowW_ = std::max(1, right - windowX_);
        windowH_ = std::max(1, bottom - windowY_);
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

    const int screen = DefaultScreen(display_);
    const bool trueType = TextRaster::Instance().Ready();

    std::vector<RenderedText> rendered;

    if (trueType)
    {
        rendered.reserve(state_.texts.size());

        for (const auto& text : state_.texts)
        {
            const int size = text.fontSize > 0 ? text.fontSize : state_.fontSize;
            TextPlacement placement = PlaceText(text, size);
            const cv::Mat canvas = RasterizeText(text, size, placement, state_.outline);

            placement.box.x -= windowX_;
            placement.box.y -= windowY_;

            RenderedText entry;
            entry.box = placement.box;
            entry.color = CreateColorImage(canvas);
            entry.coverage = CreateCoverageImage(canvas);

            if (entry.color && entry.coverage)
                rendered.push_back(entry);
            else
                DestroyRendered(entry);
        }
    }

    Region shape = XCreateRegion();

    auto addShape = [&](int x, int y, int w, int h)
        {
            XRectangle rect{
                static_cast<short>(x),
                static_cast<short>(y),
                static_cast<unsigned short>(std::max(1, w)),
                static_cast<unsigned short>(std::max(1, h))
            };

            XUnionRectWithRegion(&rect, shape, shape);
        };

    for (const RenderedText& text : rendered)
        addShape(text.box.x, text.box.y, text.box.width, text.box.height);

    for (const OverlayMark& mark : state_.marks)
    {
        addShape(mark.x - windowX_, mark.y - windowY_, mark.width, 2);
        addShape(mark.x - windowX_, mark.y - windowY_ + mark.height - 2, mark.width, 2);
        addShape(mark.x - windowX_, mark.y - windowY_, 2, mark.height);
        addShape(mark.x - windowX_ + mark.width - 2, mark.y - windowY_, 2, mark.height);
    }

    if (state_.previewEnabled)
        addShape(0, 0, windowW_, windowH_);

    if (state_.background)
    {
        XShapeCombineRegion(display_, window_, ShapeBounding, 0, 0, shape, ShapeSet);
    }
    else
    {
        Pixmap mask = XCreatePixmap(
            display_,
            window_,
            static_cast<unsigned int>(std::max(1, windowW_)),
            static_cast<unsigned int>(std::max(1, windowH_)),
            1);

        GC maskGc = XCreateGC(display_, mask, 0, nullptr);

        XSetForeground(display_, maskGc, 0);
        XFillRectangle(display_, mask, maskGc, 0, 0,
            static_cast<unsigned int>(std::max(1, windowW_)),
            static_cast<unsigned int>(std::max(1, windowH_)));

        XSetForeground(display_, maskGc, 1);

        if (trueType)
        {
            XSetFunction(display_, maskGc, GXor);

            for (const RenderedText& text : rendered)
            {
                XPutImage(
                    display_,
                    mask,
                    maskGc,
                    text.coverage,
                    0,
                    0,
                    text.box.x,
                    text.box.y,
                    static_cast<unsigned int>(text.box.width),
                    static_cast<unsigned int>(text.box.height)
                );
            }

            XSetFunction(display_, maskGc, GXcopy);
        }
        else
        {
            for (const auto& text : state_.texts)
            {
                const std::string& narrow = text.text;
                const int size = text.fontSize > 0 ? text.fontSize : state_.fontSize;

                if (XFontStruct* font = FontForSize(size))
                    XSetFont(display_, maskGc, font->fid);

                const int maskX = text.x - windowX_;
                const int maskY = text.y - windowY_ + size / 3;

                XDrawString(display_, mask, maskGc, maskX, maskY, narrow.c_str(), static_cast<int>(narrow.size()));

                if (state_.outline)
                {
                    for (const auto& [dx, dy] : kOutlineOffsets)
                    {
                        XDrawString(
                            display_,
                            mask,
                            maskGc,
                            maskX + dx,
                            maskY + dy,
                            narrow.c_str(),
                            static_cast<int>(narrow.size()));
                    }
                }
            }
        }

        XSetLineAttributes(display_, maskGc, kMarkThickness, LineSolid, CapButt, JoinMiter);

        for (const OverlayMark& mark : state_.marks)
        {
            XDrawRectangle(
                display_,
                mask,
                maskGc,
                mark.x - windowX_,
                mark.y - windowY_,
                static_cast<unsigned int>(std::max(1, mark.width - 1)),
                static_cast<unsigned int>(std::max(1, mark.height - 1)));
        }

        if (state_.previewEnabled)
        {
            XDrawRectangle(display_, mask, maskGc, 0, 0,
                static_cast<unsigned int>(std::max(1, windowW_ - 1)),
                static_cast<unsigned int>(std::max(1, windowH_ - 1)));
        }

        XShapeCombineMask(display_, window_, ShapeBounding, 0, 0, mask, ShapeSet);

        XFreeGC(display_, maskGc);
        XFreePixmap(display_, mask);
    }

    XDestroyRegion(shape);

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

    for (const OverlayMark& mark : state_.marks)
    {
        XSetForeground(display_, gc_, XColorFromOverlayColor(display_, mark.color));
        XSetLineAttributes(display_, gc_, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(
            display_,
            window_,
            gc_,
            mark.x - windowX_,
            mark.y - windowY_,
            static_cast<unsigned int>(std::max(1, mark.width - 1)),
            static_cast<unsigned int>(std::max(1, mark.height - 1))
        );
    }

    XSetLineAttributes(display_, gc_, 1, LineSolid, CapButt, JoinMiter);

    if (trueType)
    {
        for (RenderedText& text : rendered)
        {
            const Pixmap clip = XCreatePixmap(
                display_,
                window_,
                static_cast<unsigned int>(text.box.width),
                static_cast<unsigned int>(text.box.height),
                1
            );

            GC clipGc = XCreateGC(display_, clip, 0, nullptr);

            XPutImage(
                display_,
                clip,
                clipGc,
                text.coverage,
                0,
                0,
                0,
                0,
                static_cast<unsigned int>(text.box.width),
                static_cast<unsigned int>(text.box.height)
            );

            XSetClipMask(display_, gc_, clip);
            XSetClipOrigin(display_, gc_, text.box.x, text.box.y);

            XPutImage(
                display_,
                window_,
                gc_,
                text.color,
                0,
                0,
                text.box.x,
                text.box.y,
                static_cast<unsigned int>(text.box.width),
                static_cast<unsigned int>(text.box.height)
            );

            XSetClipMask(display_, gc_, None);

            XFreeGC(display_, clipGc);
            XFreePixmap(display_, clip);

            DestroyRendered(text);
        }
    }
    else
    {
        for (const auto& text : state_.texts)
        {
            const std::string& narrow = text.text;
            const int size = text.fontSize > 0 ? text.fontSize : state_.fontSize;

            if (XFontStruct* font = FontForSize(size))
                XSetFont(display_, gc_, font->fid);

            const int baseX = text.x - windowX_;
            const int baseY = text.y - windowY_ + size / 3;

            if (state_.outline)
            {
                XSetForeground(display_, gc_, BlackPixel(display_, screen));

                for (const auto& [dx, dy] : kOutlineOffsets)
                {
                    XDrawString(
                        display_,
                        window_,
                        gc_,
                        baseX + dx,
                        baseY + dy,
                        narrow.c_str(),
                        static_cast<int>(narrow.size())
                    );
                }
            }

            XSetForeground(display_, gc_, XColorFromOverlayColor(display_, text.color));
            XDrawString(
                display_,
                window_,
                gc_,
                baseX,
                baseY,
                narrow.c_str(),
                static_cast<int>(narrow.size())
            );
        }
    }

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
