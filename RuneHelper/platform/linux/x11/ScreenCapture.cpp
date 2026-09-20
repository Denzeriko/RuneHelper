#include "platform/linux/ScreenCapture.h"

#include <cstdlib>
#include <string>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <opencv2/imgproc.hpp>

#include "core/Logger.h"

namespace
{
bool IsX11Session()
{
    static const bool value = []
    {
        const char* sessionType = std::getenv("XDG_SESSION_TYPE");
        return !(sessionType && std::string(sessionType) == "wayland");
    }();

    return value;
}

class DisplayConnection
{
public:
    ~DisplayConnection()
    {
        if (display_)
            XCloseDisplay(display_);
    }

    Display* Get()
    {
        if (!display_)
            display_ = XOpenDisplay(nullptr);

        return display_;
    }

    void Drop()
    {
        if (display_)
        {
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }

private:
    Display* display_ = nullptr;
};

DisplayConnection& Connection()
{
    static DisplayConnection connection;
    return connection;
}

bool gSawXCaptureError = false;
bool gReportedXCaptureError = false;

void LogXwaylandHint()
{
    const char* wayland = std::getenv("WAYLAND_DISPLAY");

    if (!wayland || !*wayland)
        return;

    LOG_ERROR(
        "This looks like Xwayland: the X11 build cannot capture the screen on a Wayland compositor. "
        "Use the Wayland build of RuneHelper instead."
    );
}

int TrapXCaptureError(Display*, XErrorEvent* error)
{
    gSawXCaptureError = true;

    if (gReportedXCaptureError)
        return 0;

    gReportedXCaptureError = true;

    LogXwaylandHint();

    LOG_ERROR(
        "Linux screen capture: X error, code " + std::to_string(static_cast<int>(error->error_code)) +
        ", request " + std::to_string(static_cast<int>(error->request_code)) +
        " (further capture errors are not repeated until a capture succeeds)"
    );

    return 0;
}

struct ChannelLayout
{
    int shift = 0;
    unsigned long maxValue = 0;
};

ChannelLayout MakeChannelLayout(unsigned long mask)
{
    ChannelLayout layout;

    if (!mask)
        return layout;

    unsigned long bits = mask;

    while ((bits & 1UL) == 0)
    {
        bits >>= 1;
        ++layout.shift;
    }

    int width = 0;

    while (bits)
    {
        width += static_cast<int>(bits & 1UL);
        bits >>= 1;
    }

    layout.maxValue = (1UL << width) - 1UL;

    return layout;
}

unsigned char ScaleChannel(unsigned long pixel, unsigned long mask, const ChannelLayout& layout)
{
    if (!layout.maxValue)
        return 0;

    return static_cast<unsigned char>((((pixel & mask) >> layout.shift) * 255UL) / layout.maxValue);
}

bool IsPackedBgr(const XImage& image)
{
    return image.byte_order == LSBFirst &&
           image.red_mask == 0x00ff0000UL &&
           image.green_mask == 0x0000ff00UL &&
           image.blue_mask == 0x000000ffUL;
}

cv::Mat ToGray(const XImage& image, const cv::Size& size)
{
    if (IsPackedBgr(image))
    {
        if (image.bits_per_pixel == 32)
        {
            const cv::Mat wrapped(
                size.height,
                size.width,
                CV_8UC4,
                image.data,
                static_cast<std::size_t>(image.bytes_per_line)
            );

            cv::Mat result;
            cv::cvtColor(wrapped, result, cv::COLOR_BGRA2GRAY);

            return result;
        }

        if (image.bits_per_pixel == 24)
        {
            const cv::Mat wrapped(
                size.height,
                size.width,
                CV_8UC3,
                image.data,
                static_cast<std::size_t>(image.bytes_per_line)
            );

            cv::Mat result;
            cv::cvtColor(wrapped, result, cv::COLOR_BGR2GRAY);

            return result;
        }
    }

    const ChannelLayout blue = MakeChannelLayout(image.blue_mask);
    const ChannelLayout green = MakeChannelLayout(image.green_mask);
    const ChannelLayout red = MakeChannelLayout(image.red_mask);

    cv::Mat bgr(size.height, size.width, CV_8UC3);

    for (int y = 0; y < size.height; ++y)
    {
        cv::Vec3b* row = bgr.ptr<cv::Vec3b>(y);

        for (int x = 0; x < size.width; ++x)
        {
            const unsigned long pixel = XGetPixel(const_cast<XImage*>(&image), x, y);

            row[x][0] = ScaleChannel(pixel, image.blue_mask, blue);
            row[x][1] = ScaleChannel(pixel, image.green_mask, green);
            row[x][2] = ScaleChannel(pixel, image.red_mask, red);
        }
    }

    cv::Mat result;
    cv::cvtColor(bgr, result, cv::COLOR_BGR2GRAY);

    return result;
}

cv::Mat CaptureRootRegion(Display* display, const cv::Rect& region)
{
    Window root = DefaultRootWindow(display);

    XWindowAttributes attrs{};
    if (!XGetWindowAttributes(display, root, &attrs))
    {
        LOG_ERROR("Linux screen capture failed: unable to read X11 root window attributes");
        return {};
    }

    cv::Rect bounds(0, 0, attrs.width, attrs.height);
    cv::Rect clipped = region & bounds;

    if (clipped.empty())
    {
        LOG_ERROR("Linux screen capture failed: requested region is outside the X11 root window");
        return {};
    }

    gSawXCaptureError = false;
    XErrorHandler previousErrorHandler = XSetErrorHandler(TrapXCaptureError);

    XImage* image = XGetImage(
        display,
        root,
        clipped.x,
        clipped.y,
        static_cast<unsigned int>(clipped.width),
        static_cast<unsigned int>(clipped.height),
        AllPlanes,
        ZPixmap
    );

    XSync(display, False);
    XSetErrorHandler(previousErrorHandler);

    if (gSawXCaptureError || !image)
    {
        if (image)
            XDestroyImage(image);

        if (!gReportedXCaptureError)
        {
            gReportedXCaptureError = true;
            LOG_ERROR("Linux screen capture failed: XGetImage did not return an image");
            LogXwaylandHint();
        }

        return {};
    }

    gReportedXCaptureError = false;

    cv::Mat result = ToGray(*image, cv::Size(clipped.width, clipped.height));

    XDestroyImage(image);

    return result;
}
}

cv::Mat CaptureRegion(const cv::Rect& region)
{
    if (!IsX11Session())
    {
        LOG_ERROR("Linux region capture requires an X11 session; Wayland is not supported yet");
        return {};
    }

    Display* display = Connection().Get();

    if (!display)
    {
        LOG_ERROR("Linux region capture requires X11, but XOpenDisplay failed");
        return {};
    }

    cv::Mat result = CaptureRootRegion(display, region);

    if (result.empty() && XConnectionNumber(display) < 0)
        Connection().Drop();

    return result;
}
