#include "ocr/OcrFrameDiffer.h"

#include <utility>

#include <opencv2/imgproc.hpp>

namespace
{
constexpr double kOcrPixelDiffThreshold = 8.0;
constexpr double kOcrChangedPixelRatioThreshold = 0.002;

bool SimilarFrames(const cv::Mat& a, const cv::Mat& b)
{
    if (a.empty() || b.empty() || a.size() != b.size() || a.type() != b.type())
        return false;

    cv::Mat diff;
    cv::Mat changed;
    cv::absdiff(a, b, diff);
    cv::threshold(diff, changed, kOcrPixelDiffThreshold, 255, cv::THRESH_BINARY);

    const double changedPixels = static_cast<double>(cv::countNonZero(changed));
    const double totalPixels = static_cast<double>(a.total());

    return totalPixels > 0.0 && (changedPixels / totalPixels) < kOcrChangedPixelRatioThreshold;
}
}

bool OcrFrameDiffer::IsSettled(const cv::Mat& gray) const
{
    return SimilarFrames(gray, lastGray_);
}

bool OcrFrameDiffer::ChangedSinceOcr(const cv::Mat& gray) const
{
    return !SimilarFrames(gray, lastOcrGray_);
}

void OcrFrameDiffer::StoreFrame(cv::Mat gray)
{
    lastGray_ = std::move(gray);
}

void OcrFrameDiffer::StoreOcrFrame(const cv::Mat& gray)
{
    lastOcrGray_ = gray.clone();
}

void OcrFrameDiffer::Reset()
{
    lastGray_.release();
    lastOcrGray_.release();
}
