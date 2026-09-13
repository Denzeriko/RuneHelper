#pragma once

#include <cstddef>
#include <mutex>
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

class RunePatternMatcher
{
public:
    void SetSearchScale(double scale);

    void BeginScaleCalibration();
    void StepScaleCalibration(const cv::Mat& sourceGray, double threshold = 0.70);
    RunePatternCalibrationStatus CalibrationStatus() const;

    std::vector<RunePatternMatch> Find(const cv::Mat& sourceGray, double threshold = 0.70);

private:
    struct Template
    {
        std::string name;
        std::string label;
        cv::Mat gray;
    };

    struct Calibration
    {
        bool running = false;
        int attempts = 0;
        int index = 0;

        double currentScale = 0.0;
        double bestScale = 0.0;
        std::size_t bestMatches = 0;
        double bestScoreSum = -1.0;

        void Restart();
    };

    void EnsureTemplates();
    const std::vector<cv::Mat>& ScaledTemplates(double scale);
    std::vector<RunePatternMatch> FindAtScale(const cv::Mat& sourceGray, double threshold, double scale);
    double CurrentSearchScale() const;

    bool TakeNextCalibrationScale(double& scale);
    void AdvanceCalibration(double scale, std::size_t matches, double scoreSum);
    void StopCalibration(const char* reason);

    std::vector<Template> templates_;
    bool templatesLoaded_ = false;

    std::vector<cv::Mat> scaledTemplates_;
    double scaledTemplatesScale_ = -1.0;

    mutable std::mutex mutex_;
    double calibratedScale_ = 0.0;
    Calibration calibration_;
};
