#pragma once

#include <opencv2/core.hpp>

#include <stop_token>

cv::Mat CaptureRegion(const cv::Rect& region, const std::stop_token& stop);
void CancelCapture();
