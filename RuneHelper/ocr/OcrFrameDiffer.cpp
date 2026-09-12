#include "ocr/OcrFrameDiffer.h"

#include <utility>

#include <opencv2/imgproc.hpp>

namespace
{
constexpr double kOcrPixelDiffThreshold = 8.0;
constexpr double kOcrChangedPixelRatioThreshold = 0.002;
}

bool OcrFrameDiffer::IsSimilarFrame(const cv::Mat& gray, bool forceFrame)
{
    if (forceFrame)
    {
        stableFrames_ = 0;
        return false;
    }

    if (gray.empty() ||
        lastGray_.empty() ||
        gray.size() != lastGray_.size() ||
        gray.type() != lastGray_.type())
    {
        stableFrames_ = 0;
        return false;
    }

    cv::Mat diff;
    cv::Mat changed;
    cv::absdiff(gray, lastGray_, diff);
    cv::threshold(diff, changed, kOcrPixelDiffThreshold, 255, cv::THRESH_BINARY);

    const double changedPixels = static_cast<double>(cv::countNonZero(changed));
    const double totalPixels = static_cast<double>(gray.total());
    const bool similar = totalPixels > 0.0 && (changedPixels / totalPixels) < kOcrChangedPixelRatioThreshold;

    if (similar)
        ++stableFrames_;
    else
        stableFrames_ = 0;

    return similar;
}

void OcrFrameDiffer::StoreFrame(cv::Mat gray)
{
    lastGray_ = std::move(gray);
}

void OcrFrameDiffer::Reset()
{
    lastGray_.release();
    stableFrames_ = 0;
}

int OcrFrameDiffer::StableFrames() const
{
    return stableFrames_;
}
