#pragma once

#include <opencv2/core.hpp>

class OcrFrameDiffer
{
public:
    bool IsSettled(const cv::Mat& gray) const;
    bool ChangedSinceOcr(const cv::Mat& gray) const;

    void StoreFrame(cv::Mat gray);
    void StoreOcrFrame(const cv::Mat& gray);
    void Reset();

private:
    cv::Mat lastGray_;
    cv::Mat lastOcrGray_;
};
