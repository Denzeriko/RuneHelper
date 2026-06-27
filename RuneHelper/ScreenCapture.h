#pragma once

#include <opencv2/opencv.hpp>

cv::Mat CaptureScreen();
cv::Mat CaptureRegion(const cv::Rect& region);