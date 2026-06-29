#include "ScreenCapture.h"

#include "core/Logger.h"

cv::Mat CaptureScreen()
{
    LOG_ERROR("Linux screen capture is not implemented");
    return {};
}

cv::Mat CaptureRegion(const cv::Rect&)
{
    LOG_ERROR("Linux region capture is not implemented");
    return {};
}
