#include "platform/linux/ScreenCapture.h"

#include <cstdlib>
#include <string>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

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

bool gSawXCaptureError = false;
bool gReportedXCaptureError = false;

void LogXwaylandHint()
{
    const char* wayland = std::getenv("WAYLAND_DISPLAY");

    if (!wayland || !*wayland)
        return;

    LOG_ERROR("This looks like Xwayland: the X11 build cannot capture the screen on a Wayland compositor. "
              "Use the Wayland build of RuneHelper instead.");
}

int TrapXCaptureError(Display*, XErrorEvent* error)
{
    gSawXCaptureError = true;

    if (gReportedXCaptureError)
        return 0;

    gReportedXCaptureError = true;

    LogXwaylandHint();

    LOG_ERROR(
        "Linux screen capture: X error, code " + std::to_string(static_cast<int>(error->error_code)) + ", request " +
        std::to_string(static_cast<int>(error->request_code)) + " (further capture errors are not repeated until a capture succeeds)"
    );

    return 0;
}

bool IsLocalDisplay(Display* display)
{
    const char* name = DisplayString(display);

    if (!name || !*name)
        return true;

    return name[0] == ':' || std::string(name).rfind("unix:", 0) == 0;
}

class SharedImage
{
public:
    bool Available(Display* display)
    {
        if (!checked_)
        {
            checked_ = true;
            available_ = IsLocalDisplay(display) && XShmQueryExtension(display) == True;

            if (available_)
                LOG_INFO("Linux screen capture: MIT-SHM is available");
            else
                LOG_INFO("Linux screen capture: MIT-SHM is unavailable, using XGetImage");
        }

        return available_;
    }

    XImage* Acquire(Display* display, Visual* visual, int depth, int width, int height)
    {
        if (image_ && image_->width == width && image_->height == height)
            return image_;

        Destroy(display);

        XImage* image = XShmCreateImage(
            display,
            visual,
            static_cast<unsigned int>(depth),
            ZPixmap,
            nullptr,
            &segment_,
            static_cast<unsigned int>(width),
            static_cast<unsigned int>(height)
        );

        if (!image)
            return nullptr;

        const std::size_t size = static_cast<std::size_t>(image->bytes_per_line) * static_cast<std::size_t>(height);

        segment_.shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);

        if (segment_.shmid < 0)
        {
            XDestroyImage(image);
            return nullptr;
        }

        void* address = shmat(segment_.shmid, nullptr, 0);

        if (address == reinterpret_cast<void*>(-1))
        {
            shmctl(segment_.shmid, IPC_RMID, nullptr);
            XDestroyImage(image);
            return nullptr;
        }

        segment_.shmaddr = static_cast<char*>(address);
        segment_.readOnly = False;
        image->data = segment_.shmaddr;

        gSawXCaptureError = false;
        XErrorHandler previous = XSetErrorHandler(TrapXCaptureError);
        const Bool attached = XShmAttach(display, &segment_);
        XSync(display, False);
        XSetErrorHandler(previous);

        shmctl(segment_.shmid, IPC_RMID, nullptr);

        if (!attached || gSawXCaptureError)
        {
            shmdt(segment_.shmaddr);
            segment_.shmaddr = nullptr;
            XDestroyImage(image);
            available_ = false;
            LOG_ERROR("Linux screen capture: MIT-SHM attach failed, falling back to XGetImage");
            return nullptr;
        }

        image_ = image;

        return image_;
    }

    void Destroy(Display* display)
    {
        if (image_)
        {
            if (display)
                XShmDetach(display, &segment_);

            XDestroyImage(image_);
            image_ = nullptr;
        }

        if (segment_.shmaddr)
        {
            shmdt(segment_.shmaddr);
            segment_.shmaddr = nullptr;
        }

        segment_ = XShmSegmentInfo{};
    }

private:
    XImage* image_ = nullptr;
    XShmSegmentInfo segment_{};
    bool checked_ = false;
    bool available_ = false;
};

class DisplayConnection
{
public:
    ~DisplayConnection() { Drop(); }

    Display* Get()
    {
        if (!display_)
            display_ = XOpenDisplay(nullptr);

        return display_;
    }

    void Drop()
    {
        if (!display_)
            return;

        shared_.Destroy(display_);
        XCloseDisplay(display_);
        display_ = nullptr;
    }

    SharedImage& Shared() { return shared_; }

private:
    Display* display_ = nullptr;
    SharedImage shared_;
};

DisplayConnection& Connection()
{
    static DisplayConnection connection;
    return connection;
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
    return image.byte_order == LSBFirst && image.red_mask == 0x00ff0000UL && image.green_mask == 0x0000ff00UL &&
           image.blue_mask == 0x000000ffUL;
}

cv::Mat ToGray(const XImage& image, const cv::Size& size)
{
    if (IsPackedBgr(image))
    {
        if (image.bits_per_pixel == 32)
        {
            const cv::Mat wrapped(size.height, size.width, CV_8UC4, image.data, static_cast<std::size_t>(image.bytes_per_line));

            cv::Mat result;
            cv::cvtColor(wrapped, result, cv::COLOR_BGRA2GRAY);

            return result;
        }

        if (image.bits_per_pixel == 24)
        {
            const cv::Mat wrapped(size.height, size.width, CV_8UC3, image.data, static_cast<std::size_t>(image.bytes_per_line));

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

cv::Mat CaptureRootRegion(Display* display, SharedImage& shared, const cv::Rect& region)
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

    const cv::Size size(clipped.width, clipped.height);

    gSawXCaptureError = false;
    XErrorHandler previousErrorHandler = XSetErrorHandler(TrapXCaptureError);

    cv::Mat result;

    if (shared.Available(display))
    {
        if (XImage* image = shared.Acquire(display, attrs.visual, attrs.depth, clipped.width, clipped.height))
        {
            gSawXCaptureError = false;

            if (XShmGetImage(display, root, image, clipped.x, clipped.y, AllPlanes))
            {
                XSync(display, False);

                if (!gSawXCaptureError)
                    result = ToGray(*image, size);
            }
        }
    }

    if (result.empty())
    {
        gSawXCaptureError = false;

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

        if (!gSawXCaptureError && image)
            result = ToGray(*image, size);

        if (image)
            XDestroyImage(image);
    }

    XSetErrorHandler(previousErrorHandler);

    if (result.empty())
    {
        if (!gReportedXCaptureError)
        {
            gReportedXCaptureError = true;
            LOG_ERROR("Linux screen capture failed: neither XShmGetImage nor XGetImage returned an image");
            LogXwaylandHint();
        }

        return {};
    }

    gReportedXCaptureError = false;

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

    cv::Mat result = CaptureRootRegion(display, Connection().Shared(), region);

    if (result.empty() && XConnectionNumber(display) < 0)
        Connection().Drop();

    return result;
}

void CancelCapture() {}
