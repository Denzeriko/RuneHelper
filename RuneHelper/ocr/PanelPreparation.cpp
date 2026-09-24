#include "ocr/PanelPreparation.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
constexpr int kMaxNativeWidth = 750;
constexpr int kReducedWidth = 680;

constexpr double kReferenceTextP25 = 144.0;
constexpr double kReferenceTextP50 = 165.0;
constexpr double kReferenceTextP95 = 190.0;
constexpr double kTextLevelTolerance = 12.0;
constexpr double kHeldLevelTolerance = 6.0;

constexpr int kMinPanelSide = 64;
constexpr int kMinPanelEdgeContrast = 12;
constexpr int kPanelEdgeSpan = 3;
constexpr double kPanelEdgeShare = 0.30;
constexpr double kPanelLeftEdgeShare = 0.50;
constexpr float kPanelRowDensity = 0.40f;
constexpr double kPanelMinMargin = 0.05;
constexpr int kHeldPanelTolerance = 2;

struct PanelEdges
{
    cv::Mat darkening;
    cv::Mat darkeningPerColumn;
    cv::Mat brighteningPerColumn;
    int width = 0;
};

struct RowSpan
{
    int top = -1;
    int bottom = -1;
};

TextLevels MeasureTextLevels(const cv::Mat& gray)
{
    const int left = gray.cols / 2;
    const int right = gray.cols * 85 / 100;
    const int top = gray.rows / 10;
    const int bottom = gray.rows * 9 / 10;

    std::array<int, 256> histogram{};

    for (int y = top; y < bottom; ++y)
    {
        const unsigned char* row = gray.ptr<unsigned char>(y);

        for (int x = left; x < right; ++x)
            ++histogram[row[x]];
    }

    const double total = static_cast<double>(right - left) * (bottom - top);

    auto percentile = [&histogram, total](double share)
    {
        double seen = 0.0;

        for (int level = 0; level < 256; ++level)
        {
            seen += histogram[level];

            if (seen >= share * total)
                return static_cast<double>(level);
        }

        return 255.0;
    };

    return { percentile(0.25), percentile(0.50), percentile(0.95) };
}

bool CloseLevels(const TextLevels& a, const TextLevels& b)
{
    return std::abs(a.p25 - b.p25) <= kHeldLevelTolerance && std::abs(a.p50 - b.p50) <= kHeldLevelTolerance &&
           std::abs(a.p95 - b.p95) <= kHeldLevelTolerance;
}

int EdgeThreshold(const cv::Mat& gray)
{
    std::array<int, 256> histogram{};

    for (int y = 0; y < gray.rows; ++y)
    {
        const unsigned char* row = gray.ptr<unsigned char>(y);

        for (int x = 0; x < gray.cols; ++x)
            ++histogram[row[x]];
    }

    auto percentile = [&histogram, &gray](double share)
    {
        const double wanted = share * static_cast<double>(gray.total());
        double seen = 0.0;

        for (int level = 0; level < 256; ++level)
        {
            seen += histogram[level];

            if (seen >= wanted)
                return level;
        }

        return 255;
    };

    return std::max(kMinPanelEdgeContrast, (percentile(0.95) - percentile(0.05)) / 5);
}

int StrongestColumn(const int* counts, int from, int to)
{
    int best = -1;

    for (int x = std::max(0, from); x < to; ++x)
    {
        if (best < 0 || counts[x] > counts[best])
            best = x;
    }

    return best;
}

PanelEdges MeasurePanelEdges(const cv::Mat& gray)
{
    const int threshold = EdgeThreshold(gray);

    cv::Mat smooth;
    cv::blur(gray, smooth, cv::Size(2 * kPanelEdgeSpan + 1, 1));

    PanelEdges edges;
    edges.width = gray.cols - 2 * kPanelEdgeSpan;

    cv::Mat change;
    cv::subtract(
        smooth(cv::Rect(0, 0, edges.width, gray.rows)),
        smooth(cv::Rect(2 * kPanelEdgeSpan, 0, edges.width, gray.rows)),
        change,
        cv::noArray(),
        CV_16S
    );

    edges.darkening = change > threshold;
    const cv::Mat brightening = change < -threshold;

    cv::reduce(edges.darkening, edges.darkeningPerColumn, 0, cv::REDUCE_SUM, CV_32S);
    cv::reduce(brightening, edges.brighteningPerColumn, 0, cv::REDUCE_SUM, CV_32S);

    return edges;
}

RowSpan EdgeRowSpan(const cv::Mat& darkening, int column, int width)
{
    std::vector<float> edgeRows(static_cast<std::size_t>(darkening.rows), 0.0f);

    for (int y = 0; y < darkening.rows; ++y)
    {
        const unsigned char* row = darkening.ptr<unsigned char>(y);

        for (int x = std::max(0, column - 1); x <= std::min(width - 1, column + 1); ++x)
        {
            if (row[x] != 0)
                edgeRows[static_cast<std::size_t>(y)] = 1.0f;
        }
    }

    cv::Mat density;
    cv::blur(
        cv::Mat(darkening.rows, 1, CV_32F, edgeRows.data()),
        density,
        cv::Size(1, std::max(8, darkening.rows / 16)),
        cv::Point(-1, -1),
        cv::BORDER_REPLICATE
    );

    RowSpan span;

    for (int y = 0; y < darkening.rows; ++y)
    {
        if (density.at<float>(y) < kPanelRowDensity)
            continue;

        if (span.top < 0)
            span.top = y;

        span.bottom = y;
    }

    return span;
}
}

cv::Rect FindPanel(const cv::Mat& gray)
{
    const cv::Rect whole(0, 0, gray.cols, gray.rows);

    if (gray.cols < kMinPanelSide || gray.rows < kMinPanelSide)
        return whole;

    const PanelEdges edges = MeasurePanelEdges(gray);
    const int* darkeningRows = edges.darkeningPerColumn.ptr<int>(0);
    const int* brighteningRows = edges.brighteningPerColumn.ptr<int>(0);

    const int right = StrongestColumn(darkeningRows, edges.width * 55 / 100, edges.width);

    if (right < 0 || darkeningRows[right] < kPanelEdgeShare * 255.0 * gray.rows)
        return whole;

    const RowSpan span = EdgeRowSpan(edges.darkening, right, edges.width);

    if (span.top < 0)
        return whole;

    const int left = StrongestColumn(brighteningRows, 0, edges.width / 4);
    const bool framedLeft = left >= 0 && brighteningRows[left] >= kPanelLeftEdgeShare * darkeningRows[right];

    const int panelLeft = framedLeft ? left + kPanelEdgeSpan : 0;
    const int panelRight = right + kPanelEdgeSpan;

    const int sideMargin = static_cast<int>(gray.cols * kPanelMinMargin);
    const int endMargin = static_cast<int>(gray.rows * kPanelMinMargin);

    const int cropLeft = panelLeft >= sideMargin ? panelLeft : 0;
    const int cropRight = gray.cols - panelRight >= sideMargin ? panelRight : gray.cols;
    const int cropTop = span.top >= endMargin ? span.top : 0;
    const int cropBottom = gray.rows - 1 - span.bottom >= endMargin ? span.bottom + 1 : gray.rows;

    return cv::Rect(cropLeft, cropTop, cropRight - cropLeft, cropBottom - cropTop) & whole;
}

bool ClosePanels(const cv::Rect& a, const cv::Rect& b)
{
    return std::abs(a.x - b.x) <= kHeldPanelTolerance && std::abs(a.y - b.y) <= kHeldPanelTolerance &&
           std::abs(a.br().x - b.br().x) <= kHeldPanelTolerance && std::abs(a.br().y - b.br().y) <= kHeldPanelTolerance;
}

double ReadingScale(int width)
{
    return width > kMaxNativeWidth ? static_cast<double>(kReducedWidth) / width : 1.0;
}

cv::Mat NormalizeTextLevels(const cv::Mat& gray, const TextLevels& levels)
{
    if (levels.p95 <= levels.p25)
        return gray;

    const double gain = (kReferenceTextP95 - kReferenceTextP25) / (levels.p95 - levels.p25);

    cv::Mat lut(1, 256, CV_8U);

    for (int level = 0; level < 256; ++level)
        lut.at<unsigned char>(level) = cv::saturate_cast<unsigned char>(kReferenceTextP25 + (level - levels.p25) * gain);

    cv::Mat normalized;
    cv::LUT(gray, lut, normalized);

    return normalized;
}

PreparedGray PrepareGray(const cv::Mat& source, const std::optional<TextLevels>& held)
{
    PreparedGray prepared;
    prepared.gray = source;

    if (source.cols > kMaxNativeWidth)
    {
        cv::Mat reduced;
        prepared.scale = ReadingScale(source.cols);
        cv::resize(source, reduced, cv::Size(), prepared.scale, prepared.scale, cv::INTER_AREA);

        prepared.gray = reduced;
        prepared.scaled = true;
    }

    prepared.levels = MeasureTextLevels(prepared.gray);

    if (held && CloseLevels(prepared.levels, *held))
    {
        prepared.levels = *held;
    }
    else
    {
        const bool drifted = std::abs(prepared.levels.p50 - kReferenceTextP50) > kTextLevelTolerance ||
                             std::abs(prepared.levels.p95 - kReferenceTextP95) > kTextLevelTolerance;

        if (!drifted || prepared.levels.p95 <= prepared.levels.p25)
            return prepared;
    }

    prepared.gray = NormalizeTextLevels(prepared.gray, prepared.levels);
    prepared.normalized = true;

    return prepared;
}
