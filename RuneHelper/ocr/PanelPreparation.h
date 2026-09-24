#pragma once

#include <opencv2/core.hpp>

#include <optional>

struct TextLevels
{
    double p25 = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
};

struct PreparedGray
{
    cv::Mat gray;
    double scale = 1.0;
    bool scaled = false;
    bool normalized = false;
    TextLevels levels;
};

cv::Rect FindPanel(const cv::Mat& gray);
bool ClosePanels(const cv::Rect& a, const cv::Rect& b);

double ReadingScale(int width);
cv::Mat NormalizeTextLevels(const cv::Mat& gray, const TextLevels& levels);
PreparedGray PrepareGray(const cv::Mat& source, const std::optional<TextLevels>& held);
