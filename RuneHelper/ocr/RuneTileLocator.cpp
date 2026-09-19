#include "ocr/RuneTileLocator.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kBrightLevel = 180;
constexpr double kBandRowCoverage = 0.10;
constexpr int kMinBandHeight = 20;

constexpr double kStripThresholdShare = 0.25;
constexpr int kStripFlatRunToStop = 25;

constexpr double kSquareLow = 0.6;
constexpr double kSquareHigh = 1.5;

constexpr double kMaxSeamToCentreSpread = 0.5;
constexpr int kSeamHalfWidth = 1;
constexpr int kCentreHalfWidth = 2;

constexpr int kMaxTilesPerRow = 12;

double ColumnSpread(const cv::Mat& gray, int x, const RuneTileBand& band)
{
    double sum = 0.0;
    double sumSquares = 0.0;

    const int height = band.Height();

    for (int y = band.top; y <= band.bottom; ++y)
    {
        const double value = gray.at<unsigned char>(y, x);
        sum += value;
        sumSquares += value * value;
    }

    const double mean = sum / height;
    const double variance = sumSquares / height - mean * mean;

    return variance > 0.0 ? std::sqrt(variance) : 0.0;
}
}

bool RuneTileLocator::Analyze(const cv::Mat& gray)
{
    valid_ = false;
    bands_.clear();

    if (gray.empty() || gray.type() != CV_8UC1)
        return false;

    int start = -1;

    for (int y = 0; y < gray.rows; ++y)
    {
        const unsigned char* row = gray.ptr<unsigned char>(y);

        int bright = 0;

        for (int x = 0; x < gray.cols; ++x)
        {
            if (row[x] > kBrightLevel)
                ++bright;
        }

        const bool covered = static_cast<double>(bright) / gray.cols > kBandRowCoverage;

        if (covered && start < 0)
        {
            start = y;
        }
        else if (!covered && start >= 0)
        {
            if (y - start >= kMinBandHeight)
                bands_.push_back({ start, y - 1 });

            start = -1;
        }
    }

    if (start >= 0 && gray.rows - start >= kMinBandHeight)
        bands_.push_back({ start, gray.rows - 1 });

    valid_ = !bands_.empty();

    return valid_;
}

const RuneTileBand* RuneTileLocator::BandContaining(int y) const
{
    for (const RuneTileBand& band : bands_)
    {
        if (y >= band.top && y <= band.bottom)
            return &band;
    }

    return nullptr;
}

const RuneTileBand* RuneTileLocator::BandAbove(int y) const
{
    const RuneTileBand* best = nullptr;

    for (const RuneTileBand& band : bands_)
    {
        if (band.bottom >= y)
            continue;

        if (!best || band.bottom > best->bottom)
            best = &band;
    }

    return best;
}

std::vector<cv::Rect> RuneTileLocator::TilesForRow(const cv::Mat& gray, int textTop, int count) const
{
    if (const RuneTileBand* band = BandContaining(textTop))
    {
        std::vector<cv::Rect> tiles = TilesIn(gray, *band, count);

        if (!tiles.empty())
            return tiles;
    }

    if (const RuneTileBand* band = BandAbove(textTop))
        return TilesIn(gray, *band, count);

    return {};
}

bool RuneTileLocator::StripBounds(const cv::Mat& gray, int top, int bottom, int& left, int& right)
{
    if (gray.empty() || gray.type() != CV_8UC1)
        return false;

    if (top < 0 || bottom >= gray.rows || bottom - top + 1 < kMinBandHeight)
        return false;

    const RuneTileBand band{ top, bottom };

    std::vector<double> spread(gray.cols);
    double peak = 0.0;

    for (int x = 0; x < gray.cols; ++x)
    {
        spread[x] = ColumnSpread(gray, x, band);
        peak = std::max(peak, spread[x]);
    }

    if (peak <= 0.0)
        return false;

    const double threshold = peak * kStripThresholdShare;

    left = -1;

    for (int x = 0; x < gray.cols; ++x)
    {
        if (spread[x] > threshold)
        {
            left = x;
            break;
        }
    }

    if (left < 0)
        return false;

    right = left;
    int flat = 0;

    for (int x = left; x < gray.cols; ++x)
    {
        if (spread[x] > threshold)
        {
            right = x;
            flat = 0;
            continue;
        }

        if (++flat > kStripFlatRunToStop)
            break;
    }

    return right > left;
}

std::vector<cv::Rect> RuneTileLocator::TilesIn(const cv::Mat& gray, const RuneTileBand& band, int count) const
{
    if (count <= 0 || count > kMaxTilesPerRow || gray.empty() || gray.type() != CV_8UC1)
        return {};

    if (band.top < 0 || band.bottom >= gray.rows || band.Height() < kMinBandHeight)
        return {};

    int left = 0;
    int right = 0;

    if (!StripBounds(gray, band.top, band.bottom, left, right))
        return {};

    std::vector<double> spread(gray.cols);

    for (int x = 0; x < gray.cols; ++x)
        spread[x] = ColumnSpread(gray, x, band);

    const double pitch = static_cast<double>(right - left + 1) / count;
    const double height = band.Height();

    if (pitch < kSquareLow * height || pitch > kSquareHigh * height)
        return {};

    if (count > 1)
    {
        double seam = 0.0;
        double centre = 0.0;
        int seamCount = 0;
        int centreCount = 0;

        for (int i = 1; i < count; ++i)
        {
            const int boundary = left + static_cast<int>(std::lround(pitch * i));

            for (int d = -kSeamHalfWidth; d <= kSeamHalfWidth; ++d)
            {
                const int x = boundary + d;

                if (x >= 0 && x < gray.cols)
                {
                    seam += spread[x];
                    ++seamCount;
                }
            }

            const int middle = left + static_cast<int>(std::lround(pitch * (i - 0.5)));

            for (int d = -kCentreHalfWidth; d <= kCentreHalfWidth; ++d)
            {
                const int x = middle + d;

                if (x >= 0 && x < gray.cols)
                {
                    centre += spread[x];
                    ++centreCount;
                }
            }
        }

        if (seamCount == 0 || centreCount == 0)
            return {};

        const double centreMean = centre / centreCount;

        if (centreMean <= 0.0)
            return {};

        if ((seam / seamCount) / centreMean > kMaxSeamToCentreSpread)
            return {};
    }

    std::vector<cv::Rect> tiles;
    tiles.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const int x = left + static_cast<int>(std::lround(pitch * i));
        const int w = static_cast<int>(std::lround(pitch));

        if (x + w > gray.cols)
            break;

        tiles.push_back(cv::Rect(x, band.top, w, band.Height()));
    }

    return tiles;
}
