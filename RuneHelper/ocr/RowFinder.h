#pragma once

#include <opencv2/core.hpp>

#include <vector>

std::vector<cv::Rect> FindLootRows(const cv::Mat& gray);
