#include "ScreenCapture.h"

#include <stdexcept>

cv::Mat CaptureScreen()
{
    throw std::runtime_error("Linux screen capture is not implemented");
}

cv::Mat CaptureRegion(const cv::Rect&)
{
    throw std::runtime_error("Linux region capture is not implemented");
}
