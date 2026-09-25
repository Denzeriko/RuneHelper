#include "ocr/RuneTileLocator.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/imgproc.hpp>

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

constexpr int kLightingWindowRows = 41;

unsigned char UpperQuartile(std::vector<unsigned char>& values)
{
    auto quartile = values.begin() + static_cast<std::ptrdiff_t>(values.size() * 3 / 4);
    std::nth_element(values.begin(), quartile, values.end());

    return *quartile;
}

cv::Mat EvenRowLighting(const cv::Mat& view)
{
    std::vector<float> level(static_cast<std::size_t>(view.rows), 0.0f);
    std::vector<unsigned char> values(static_cast<std::size_t>(view.cols));

    for (int y = 0; y < view.rows; ++y)
    {
        const unsigned char* row = view.ptr<unsigned char>(y);
        values.assign(row, row + view.cols);

        level[static_cast<std::size_t>(y)] = UpperQuartile(values);
    }

    cv::Mat reach;
    cv::dilate(
        cv::Mat(view.rows, 1, CV_32F, level.data()),
        reach,
        cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, kLightingWindowRows)),
        cv::Point(-1, -1),
        1,
        cv::BORDER_REPLICATE
    );

    double brightest = 0.0;
    cv::minMaxLoc(reach, nullptr, &brightest);

    cv::Mat even(view.size(), CV_8UC1);

    for (int y = 0; y < view.rows; ++y)
    {
        const float local = reach.at<float>(y);
        const double gain = local > 0.0f ? brightest / local : 1.0;

        view.row(y).convertTo(even.row(y), CV_8U, gain);
    }

    return even;
}

std::vector<double> ColumnSpread(const cv::Mat& gray, const RuneTileBand& band)
{
    const int height = band.Height();
    const int cols = gray.cols;

    std::vector<double> spread(cols, 0.0);

    if (height <= 0 || cols <= 0)
        return spread;

    std::vector<double> sum(cols, 0.0);
    std::vector<double> sumSquares(cols, 0.0);

    for (int y = band.top; y <= band.bottom; ++y)
    {
        const unsigned char* row = gray.ptr<unsigned char>(y);

        for (int x = 0; x < cols; ++x)
        {
            const double value = row[x];
            sum[x] += value;
            sumSquares[x] += value * value;
        }
    }

    for (int x = 0; x < cols; ++x)
    {
        const double mean = sum[x] / height;
        const double variance = sumSquares[x] / height - mean * mean;

        spread[x] = variance > 0.0 ? std::sqrt(variance) : 0.0;
    }

    return spread;
}

bool StripBoundsFromSpread(const std::vector<double>& spread, int& left, int& right)
{
    double peak = 0.0;

    for (const double value : spread)
        peak = std::max(peak, value);

    if (peak <= 0.0)
        return false;

    const double threshold = peak * kStripThresholdShare;
    const int cols = static_cast<int>(spread.size());

    left = -1;

    for (int x = 0; x < cols; ++x)
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

    for (int x = left; x < cols; ++x)
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
}

bool RuneTileLocator::Analyze(const cv::Mat& gray, const cv::Rect& panel, const std::optional<TextLevels>& levels)
{
    valid_ = false;
    bands_.clear();

    if (gray.empty() || gray.type() != CV_8UC1)
        return false;

    const cv::Rect whole(0, 0, gray.cols, gray.rows);
    panel_ = panel & whole;

    if (panel_.empty())
        panel_ = whole;

    scale_ = ReadingScale(panel_.width);

    const cv::Mat view = EvenRowLighting(levels ? NormalizeTextLevels(PanelView(gray), *levels) : PanelView(gray));

    int start = -1;

    for (int y = 0; y < view.rows; ++y)
    {
        const unsigned char* row = view.ptr<unsigned char>(y);

        int bright = 0;

        for (int x = 0; x < view.cols; ++x)
        {
            if (row[x] > kBrightLevel)
                ++bright;
        }

        const bool covered = static_cast<double>(bright) / view.cols > kBandRowCoverage;

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

    if (start >= 0 && view.rows - start >= kMinBandHeight)
        bands_.push_back({ start, view.rows - 1 });

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
    const int y = static_cast<int>(std::lround((textTop - panel_.y) * scale_));

    if (const RuneTileBand* band = BandContaining(y))
    {
        std::vector<cv::Rect> tiles = TilesIn(gray, *band, count);

        if (!tiles.empty())
            return tiles;
    }

    if (const RuneTileBand* band = BandAbove(y))
        return TilesIn(gray, *band, count);

    return {};
}

cv::Mat RuneTileLocator::PanelView(const cv::Mat& image) const
{
    if (scale_ >= 1.0)
        return image(panel_);

    cv::Mat reduced;
    cv::resize(image(panel_), reduced, cv::Size(), scale_, scale_, cv::INTER_AREA);

    return reduced;
}

bool RuneTileLocator::StripBounds(const cv::Mat& gray, int top, int bottom, int& left, int& right)
{
    if (gray.empty() || gray.type() != CV_8UC1)
        return false;

    if (top < 0 || bottom >= gray.rows || bottom - top + 1 < kMinBandHeight)
        return false;

    return StripBoundsFromSpread(ColumnSpread(gray, RuneTileBand{ top, bottom }), left, right);
}

std::vector<cv::Rect> RuneTileLocator::TilesIn(const cv::Mat& image, const RuneTileBand& band, int count) const
{
    if (count <= 0 || count > kMaxTilesPerRow || image.empty() || image.type() != CV_8UC1)
        return {};

    if ((panel_ & cv::Rect(0, 0, image.cols, image.rows)) != panel_)
        return {};

    const cv::Mat gray = PanelView(image);

    if (band.top < 0 || band.bottom >= gray.rows || band.Height() < kMinBandHeight)
        return {};

    const std::vector<double> spread = ColumnSpread(gray, band);

    int left = 0;
    int right = 0;

    if (!StripBoundsFromSpread(spread, left, right))
        return {};

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

        tiles.push_back(cv::Rect(
            panel_.x + static_cast<int>(std::lround(x / scale_)),
            panel_.y + static_cast<int>(std::lround(band.top / scale_)),
            static_cast<int>(std::lround(w / scale_)),
            static_cast<int>(std::lround(band.Height() / scale_))
        ));
    }

    return tiles;
}
