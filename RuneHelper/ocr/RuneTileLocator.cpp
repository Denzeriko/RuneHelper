#include "ocr/RuneTileLocator.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kBrightLevel = 180;
constexpr double kBandRowCoverage = 0.10;
constexpr double kTileColumnCoverage = 0.03;
constexpr int kMinBandHeight = 20;
constexpr int kMinRunWidth = 8;
constexpr double kSquareLow = 0.7;
constexpr double kSquareHigh = 1.3;
constexpr double kAdjacentPitchFloor = 0.85;
constexpr int kMinTilesPerBand = 2;

struct Run
{
    int left = 0;
    int right = 0;

    int Width() const { return right - left + 1; }
};

double Median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());

    return values[middle];
}

std::vector<RuneTileBand> FindBands(const cv::Mat& gray)
{
    std::vector<RuneTileBand> bands;

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
                bands.push_back({ start, y - 1 });

            start = -1;
        }
    }

    if (start >= 0 && gray.rows - start >= kMinBandHeight)
        bands.push_back({ start, gray.rows - 1 });

    return bands;
}

std::vector<Run> FindRuns(const cv::Mat& gray, const RuneTileBand& band)
{
    const int height = band.bottom - band.top + 1;

    std::vector<Run> runs;
    int start = -1;

    for (int x = 0; x < gray.cols; ++x)
    {
        int bright = 0;

        for (int y = band.top; y <= band.bottom; ++y)
        {
            if (gray.at<unsigned char>(y, x) > kBrightLevel)
                ++bright;
        }

        const bool covered = static_cast<double>(bright) / height > kTileColumnCoverage;

        if (covered && start < 0)
        {
            start = x;
        }
        else if (!covered && start >= 0)
        {
            if (x - start >= kMinRunWidth)
                runs.push_back({ start, x - 1 });

            start = -1;
        }
    }

    if (start >= 0 && gray.cols - start >= kMinRunWidth)
        runs.push_back({ start, gray.cols - 1 });

    return runs;
}
}

bool RuneTileLocator::Analyze(const cv::Mat& gray)
{
    valid_ = false;
    bands_.clear();
    x0_ = 0.0;
    pitch_ = 0.0;
    tileWidth_ = 0;

    if (gray.empty() || gray.type() != CV_8UC1)
        return false;

    std::vector<double> widths;
    std::vector<double> pitches;
    std::vector<int> lefts;

    for (const RuneTileBand& band : FindBands(gray))
    {
        const int height = band.bottom - band.top + 1;
        const std::vector<Run> runs = FindRuns(gray, band);

        std::vector<Run> square;

        for (const Run& run : runs)
        {
            if (run.Width() >= kSquareLow * height && run.Width() <= kSquareHigh * height)
                square.push_back(run);
        }

        if (static_cast<int>(square.size()) < kMinTilesPerBand)
            continue;

        bands_.push_back(band);

        double smallest = 0.0;

        for (size_t i = 0; i + 1 < square.size(); ++i)
        {
            const double gap = square[i + 1].left - square[i].left;

            if (gap < kAdjacentPitchFloor * square[i].Width())
                continue;

            if (smallest <= 0.0 || gap < smallest)
                smallest = gap;
        }

        if (smallest > 0.0)
            pitches.push_back(smallest);

        for (const Run& run : square)
        {
            widths.push_back(run.Width());
            lefts.push_back(run.left);
        }
    }

    if (bands_.empty() || pitches.empty() || lefts.empty())
    {
        bands_.clear();
        return false;
    }

    tileWidth_ = static_cast<int>(std::lround(Median(widths)));
    pitch_ = Median(pitches);
    x0_ = *std::min_element(lefts.begin(), lefts.end());

    valid_ = tileWidth_ > 0 && pitch_ >= tileWidth_;

    if (!valid_)
        bands_.clear();

    return valid_;
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

cv::Rect RuneTileLocator::TileRect(const RuneTileBand& band, int index) const
{
    const int left = static_cast<int>(std::lround(x0_ + pitch_ * index));

    return cv::Rect(left, band.top, tileWidth_, band.bottom - band.top + 1);
}
