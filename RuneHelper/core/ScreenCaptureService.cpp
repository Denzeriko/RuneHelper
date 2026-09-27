#include "core/ScreenCaptureService.h"

#ifdef _WIN32
#include "platform/windows/ScreenCapture.h"
#else
#include "platform/linux/ScreenCapture.h"
#endif

cv::Mat ScreenCaptureService::CaptureRegion(const cv::Rect& region, const std::stop_token& stop)
{
#ifdef _WIN32
    if (stop.stop_requested())
        return {};

    cv::Mat img = desktopDuplication_.CaptureRegion(region);

    if (!img.empty())
        return img;

    return ::CaptureRegion(region);
#else
    return ::CaptureRegion(region, stop);
#endif
}

std::unique_ptr<IScreenCapture> CreateScreenCapture()
{
    return std::make_unique<ScreenCaptureService>();
}

void ScreenCaptureService::Cancel()
{
#ifndef _WIN32
    CancelCapture();
#endif
}

void ScreenCaptureService::Shutdown()
{
#ifdef _WIN32
    desktopDuplication_.Shutdown();
#else
    ShutdownCapture();
#endif
}
