#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

struct RunePatternMatch
{
    std::string name;
    std::string label;
    cv::Rect rect;
    double score = 0.0;
    double scale = 1.0;
};

struct RunePatternCalibrationStatus
{
    bool running = false;
    int step = 0;
    int total = 10;
    double currentScale = 0.80;
    double bestScale = 0.0;
    size_t bestMatches = 0;
};

void BeginRunePatternScaleCalibration();
RunePatternCalibrationStatus GetRunePatternCalibrationStatus();
void SetRunePatternSearchScale(double scale);
void StepRunePatternScaleCalibration(const cv::Mat& sourceGray, double threshold = 0.70);
std::vector<RunePatternMatch> FindRunePatternMatches(const cv::Mat& sourceGray, double threshold = 0.70);
