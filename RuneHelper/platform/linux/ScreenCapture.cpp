#include "ScreenCapture.h"

#include <cstdlib>
#include <string>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "core/Logger.h"

namespace
{
bool IsX11Session()
{
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");

    if (sessionType && std::string(sessionType) == "wayland")
        return false;

    return true;
}

int MaskShift(unsigned long mask)
{
    int shift = 0;

    while (mask && ((mask & 1UL) == 0))
    {
        mask >>= 1;
        ++shift;
    }

    return shift;
}

int MaskBits(unsigned long mask)
{
    int bits = 0;

    while (mask)
    {
        bits += static_cast<int>(mask & 1UL);
        mask >>= 1;
    }

    return bits;
}

unsigned char ScaleChannel(unsigned long pixel, unsigned long mask)
{
    if (!mask)
        return 0;

    const int shift = MaskShift(mask);
    const int bits = MaskBits(mask);
    const unsigned long value = (pixel & mask) >> shift;
    const unsigned long maxValue = (1UL << bits) - 1UL;

    if (!maxValue)
        return 0;

    return static_cast<unsigned char>((value * 255UL) / maxValue);
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

    if (!image)
    {
        LOG_ERROR("Linux screen capture failed: XGetImage returned null");
        return {};
    }

    cv::Mat result(clipped.height, clipped.width, CV_8UC3);

    for (int y = 0; y < clipped.height; ++y)
    {
        for (int x = 0; x < clipped.width; ++x)
        {
            unsigned long pixel = XGetPixel(image, x, y);
            cv::Vec3b& bgr = result.at<cv::Vec3b>(y, x);

            bgr[0] = ScaleChannel(pixel, image->blue_mask);
            bgr[1] = ScaleChannel(pixel, image->green_mask);
            bgr[2] = ScaleChannel(pixel, image->red_mask);
        }
    }

    XDestroyImage(image);
    return result;
}
}

cv::Mat CaptureScreen()
{
    if (!IsX11Session())
    {
        LOG_ERROR("Linux screen capture requires an X11 session; Wayland is not supported yet");
        return {};
    }

    Display* display = XOpenDisplay(nullptr);

    if (!display)
    {
        LOG_ERROR("Linux screen capture requires X11, but XOpenDisplay failed");
        return {};
    }

    Window root = DefaultRootWindow(display);
    XWindowAttributes attrs{};

    if (!XGetWindowAttributes(display, root, &attrs))
    {
        LOG_ERROR("Linux screen capture failed: unable to read X11 root window attributes");
        XCloseDisplay(display);
        return {};
    }

    cv::Mat result = CaptureRootRegion(display, cv::Rect(0, 0, attrs.width, attrs.height));
    XCloseDisplay(display);

    return result;
}

cv::Mat CaptureRegion(const cv::Rect& region)
{
    if (!IsX11Session())
    {
        LOG_ERROR("Linux region capture requires an X11 session; Wayland is not supported yet");
        return {};
    }

    Display* display = XOpenDisplay(nullptr);

    if (!display)
    {
        LOG_ERROR("Linux region capture requires X11, but XOpenDisplay failed");
        return {};
    }

    cv::Mat result = CaptureRootRegion(display, region);
    XCloseDisplay(display);

    return result;
}
