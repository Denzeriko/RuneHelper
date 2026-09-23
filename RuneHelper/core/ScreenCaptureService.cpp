#include "core/ScreenCaptureService.h"

#ifdef _WIN32
#include "platform/windows/ScreenCapture.h"
#else
#include "platform/linux/ScreenCapture.h"
#endif

cv::Mat ScreenCaptureService::CaptureRegion(const cv::Rect& region)
{
#ifdef _WIN32
    cv::Mat img = wgcCapture_.CaptureRegion(region);

    if (!img.empty())
        return img;
#endif

    return ::CaptureRegion(region);
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
    wgcCapture_.Shutdown();
#endif
}
