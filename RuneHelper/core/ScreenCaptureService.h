#pragma once

#include <opencv2/core.hpp>

#ifdef _WIN32
#include "platform/windows/ScreenCaptureDXGI.h"
#endif

class ScreenCaptureService
{
public:
    cv::Mat CaptureRegion(const cv::Rect& region);
    void Cancel();
    void Shutdown();

private:
#ifdef _WIN32
    ScreenCaptureWGC wgcCapture_;
#endif
};
