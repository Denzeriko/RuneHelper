#pragma once

#include <opencv2/core.hpp>

class OcrFrameDiffer
{
public:
    bool IsSimilarFrame(const cv::Mat& gray, bool forceFrame);
    void StoreFrame(cv::Mat gray);
    void Reset();

    int StableFrames() const;

private:
    cv::Mat lastGray_;
    int stableFrames_ = 0;
};
