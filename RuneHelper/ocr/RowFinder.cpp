#include "ocr/RowFinder.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace
{
constexpr double kTextColumnStart = 0.50;
constexpr int kInkThreshold = 115;
constexpr double kVerticalLineInkRatio = 0.95;
constexpr double kFrameInkRatio = 0.50;
constexpr int kFrameSearchPercent = 15;

constexpr int kMinInkPerRow = 12;
constexpr int kMaxBlankGap = 4;
constexpr int kMinTextBandHeight = 6;
constexpr double kMaxTextBandHeightFactor = 2.0;
constexpr int kPaddingAbovePercent = 66;
constexpr int kPaddingBelowPercent = 33;
constexpr int kMinVerticalPadding = 2;
constexpr double kMaxTextBandInkRatio = 0.25;
constexpr int kTextEdgeRowPercent = 20;
constexpr double kTextEdgeTolerance = 0.08;

using Band = std::pair<int, int>;

int BandHeight(const Band& band)
{
    return band.second - band.first + 1;
}

void EraseVerticalLines(cv::Mat& dark)
{
    cv::Mat columnSums;
    cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

    const int limit = static_cast<int>(dark.rows * 255.0 * kVerticalLineInkRatio);
    const int* sums = columnSums.ptr<int>(0);

    for (int x = 0; x < dark.cols; ++x)
    {
        if (sums[x] >= limit)
            dark.col(x).setTo(0);
    }
}

void EraseRightFrame(cv::Mat& dark)
{
    cv::Mat columnSums;
    cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

    const int limit = static_cast<int>(dark.rows * 255.0 * kFrameInkRatio);
    const int* sums = columnSums.ptr<int>(0);

    for (int x = dark.cols - dark.cols * kFrameSearchPercent / 100; x < dark.cols; ++x)
    {
        if (sums[x] >= limit)
        {
            dark.colRange(std::max(0, x - 1), dark.cols).setTo(0);
            break;
        }
    }
}

cv::Mat TextColumnInk(const cv::Mat& gray)
{
    const int textAreaX = static_cast<int>(gray.cols * kTextColumnStart);
    cv::Mat rightGray = gray(cv::Rect(textAreaX, 0, gray.cols - textAreaX, gray.rows));

    cv::Mat dark;
    cv::threshold(rightGray, dark, kInkThreshold, 255, cv::THRESH_BINARY_INV);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 2));
    cv::morphologyEx(dark, dark, cv::MORPH_CLOSE, kernel);

    EraseVerticalLines(dark);
    EraseRightFrame(dark);

    return dark;
}

std::vector<Band> FindInkBands(const cv::Mat& dark)
{
    std::vector<Band> bands;

    cv::Mat rowInk;
    cv::reduce(dark, rowInk, 1, cv::REDUCE_SUM, CV_32S);

    bool inBand = false;
    int bandStart = 0;
    int bandEnd = 0;
    int blankGap = 0;

    auto finishBand = [&]()
    {
        if (!inBand)
            return;

        bands.emplace_back(bandStart, bandEnd);
        inBand = false;
        blankGap = 0;
    };

    for (int y = 0; y < dark.rows; ++y)
    {
        const int ink = rowInk.ptr<int>(y)[0] / 255;
        if (ink >= kMinInkPerRow)
        {
            if (!inBand)
            {
                inBand = true;
                bandStart = y;
            }

            bandEnd = y;
            blankGap = 0;
            continue;
        }

        if (!inBand)
            continue;

        ++blankGap;
        if (blankGap > kMaxBlankGap)
            finishBand();
    }

    finishBand();

    return bands;
}

int RightmostInkColumn(const cv::Mat& dark, int minimumRows)
{
    cv::Mat columnSums;
    cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

    const int* sums = columnSums.ptr<int>(0);
    const int limit = minimumRows * 255;

    for (int x = columnSums.cols - 1; x >= 0; --x)
    {
        if (sums[x] >= limit)
            return x;
    }

    return -1;
}

std::vector<bool> ReachesTextEdge(const cv::Mat& dark, const std::vector<Band>& bands)
{
    std::vector<int> rightEdges(bands.size(), -1);
    int textRightEdge = -1;

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const int h = BandHeight(bands[i]);
        const int minimumRows = std::max(1, (h * kTextEdgeRowPercent + 99) / 100);

        rightEdges[i] = RightmostInkColumn(dark(cv::Rect(0, bands[i].first, dark.cols, h)), minimumRows);

        if (h >= kMinTextBandHeight)
            textRightEdge = std::max(textRightEdge, rightEdges[i]);
    }

    const int alignedFrom = textRightEdge - static_cast<int>(dark.cols * kTextEdgeTolerance);
    std::vector<bool> reaches(bands.size());

    for (std::size_t i = 0; i < bands.size(); ++i)
        reaches[i] = rightEdges[i] >= 0 && rightEdges[i] >= alignedFrom;

    return reaches;
}

int MedianTextBandHeight(const std::vector<Band>& bands, const std::vector<bool>& reaches)
{
    std::vector<int> heights;
    heights.reserve(bands.size());

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const int h = BandHeight(bands[i]);

        if (h >= kMinTextBandHeight && reaches[i])
            heights.push_back(h);
    }

    std::sort(heights.begin(), heights.end());

    return heights.empty() ? 0 : heights[heights.size() / 2];
}
}

std::vector<cv::Rect> FindLootRows(const cv::Mat& gray)
{
    std::vector<cv::Rect> rows;

    if (gray.empty())
        return rows;

    const cv::Mat dark = TextColumnInk(gray);
    const std::vector<Band> bands = FindInkBands(dark);
    const std::vector<bool> reaches = ReachesTextEdge(dark, bands);

    const int median = MedianTextBandHeight(bands, reaches);
    const int maxHeight = median > 0 ? static_cast<int>(median * kMaxTextBandHeightFactor) : gray.rows;

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const Band& band = bands[i];
        const int h = BandHeight(band);

        if (h < kMinTextBandHeight || h > maxHeight || !reaches[i])
            continue;

        const int reference = median > 0 ? median : h;
        const int padAbove = std::max(kMinVerticalPadding, reference * kPaddingAbovePercent / 100);
        const int padBelow = std::max(kMinVerticalPadding, reference * kPaddingBelowPercent / 100);

        const int y = std::max(0, band.first - padAbove);
        const int y2 = (std::min)(gray.rows, band.second + padBelow + 1);
        const cv::Rect rect(0, y, dark.cols, y2 - y);
        const double inkRatio = static_cast<double>(cv::countNonZero(dark(rect))) / rect.area();

        if (inkRatio <= kMaxTextBandInkRatio)
            rows.push_back(cv::Rect(0, y, gray.cols, y2 - y));
    }

    return rows;
}
