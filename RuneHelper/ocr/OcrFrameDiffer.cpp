#include "ocr/OcrFrameDiffer.h"

#include "ocr/OCR.h"

#include <utility>

#include <opencv2/imgproc.hpp>

namespace
{
constexpr double kOcrPixelDiffThreshold = 8.0;
constexpr double kOcrChangedPixelRatioThreshold = 0.002;
constexpr double kMissingTextChangedPixelRatio = 0.35;

bool SimilarImages(const cv::Mat& a, const cv::Mat& b, double changedPixelRatio)
{
    if (a.empty() || b.empty() || a.size() != b.size() || a.type() != b.type())
        return false;

    cv::Mat diff;
    cv::Mat changed;
    cv::absdiff(a, b, diff);
    cv::threshold(diff, changed, kOcrPixelDiffThreshold, 255, cv::THRESH_BINARY);

    const double changedPixels = static_cast<double>(cv::countNonZero(changed));
    const double totalPixels = static_cast<double>(a.total());

    return totalPixels > 0.0 && (changedPixels / totalPixels) < changedPixelRatio;
}

}

bool SimilarImages(const cv::Mat& a, const cv::Mat& b)
{
    return SimilarImages(a, b, kOcrChangedPixelRatioThreshold);
}

bool OcrFrameDiffer::OcrTextDisappeared(const cv::Mat& gray, const std::vector<LootLine>& loot) const
{
    if (loot.empty() || lastGray_.empty() || lastOcrGray_.empty())
        return false;

    const auto visible = [&](const cv::Mat& image)
    {
        if (image.size() != lastOcrGray_.size() || image.type() != lastOcrGray_.type())
            return false;

        const cv::Rect bounds(0, 0, image.cols, image.rows);
        for (const LootLine& line : loot)
        {
            const cv::Rect text = cv::Rect(line.x1, line.y1, line.x2 - line.x1, line.y2 - line.y1) & bounds;
            if (!text.empty() && SimilarImages(image(text), lastOcrGray_(text), kMissingTextChangedPixelRatio))
                return true;
        }
        return false;
    };

    return !visible(gray) && !visible(lastGray_);
}

bool OcrFrameDiffer::IsSettled(const cv::Mat& gray) const
{
    return SimilarImages(gray, lastGray_);
}

bool OcrFrameDiffer::ChangedSinceOcr(const cv::Mat& gray) const
{
    return !SimilarImages(gray, lastOcrGray_);
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
