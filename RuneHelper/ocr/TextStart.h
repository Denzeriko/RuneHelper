#pragma once

#include <opencv2/core.hpp>

#include <vector>

std::vector<int> FindTextStartX(const cv::Mat& gray, const std::vector<cv::Rect>& rows);
