#pragma once

#include <optional>
#include <vector>

#include <opencv2/core.hpp>

#include "ocr/OCR.h"

struct RuneTileBand
{
    int top = 0;
    int bottom = 0;

    int Height() const { return bottom - top + 1; }
};

class RuneTileLocator
{
public:
    bool Analyze(const cv::Mat& gray, const cv::Rect& panel, const std::optional<TextLevels>& levels);

    bool Valid() const { return valid_; }

    std::vector<cv::Rect> TilesForRow(const cv::Mat& gray, int textTop, int count) const;

    static bool StripBounds(const cv::Mat& gray, int top, int bottom, int& left, int& right);

private:
    const RuneTileBand* BandContaining(int y) const;
    const RuneTileBand* BandAbove(int y) const;
    std::vector<cv::Rect> TilesIn(const cv::Mat& image, const RuneTileBand& band, int count) const;
    cv::Mat PanelView(const cv::Mat& image) const;

    bool valid_ = false;
    cv::Rect panel_;
    double scale_ = 1.0;
    std::vector<RuneTileBand> bands_;
};
