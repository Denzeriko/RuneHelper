#include "ocr/TextStart.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace
{
constexpr double kGapBias = 0.5;
constexpr double kTileFrameInk = 0.8;
constexpr double kTextInk = 0.2;
constexpr double kMinTextCoverage = 0.08;
constexpr int kMaxTileGapPercent = 3;
constexpr int kFirstTileGapPercent = 25;
constexpr int kMinTilePitchPercent = 4;
constexpr int kMaxTilePitchPercent = 14;
constexpr int kTileSlack = 2;
constexpr int kTileEdge = 2;

struct Gap
{
    int start = -1;
    int width = 0;
};

struct TileGap
{
    int blankStart = 0;
    int blankEnd = 0;
    int frame = 0;
    bool closed = false;

    int Blank() const { return blankEnd - blankStart + 1; }
};

struct TileChain
{
    const TileGap* end = nullptr;
    int count = 0;
};

struct TileGrid
{
    int count = 0;
    int lastRight = 0;
    int blank = 0;
};

struct LastTile
{
    int right = -1;
    int blank = 0;
};

std::vector<int> ColumnInk(const cv::Mat& dark)
{
    cv::Mat columnSums;
    cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

    const int* sums = columnSums.ptr<int>(0);
    std::vector<int> ink(static_cast<std::size_t>(columnSums.cols));

    for (int x = 0; x < columnSums.cols; ++x)
        ink[x] = sums[x] / 255;

    return ink;
}

int LastInkColumn(const std::vector<int>& ink)
{
    for (int x = static_cast<int>(ink.size()) - 1; x >= 0; --x)
    {
        if (ink[x] != 0)
            return x;
    }

    return -1;
}

Gap WidestGap(const std::vector<int>& ink, int lastInk)
{
    Gap best;
    int runStart = -1;

    for (int x = 0; x <= lastInk; ++x)
    {
        if (ink[x] == 0)
        {
            if (runStart < 0)
                runStart = x;

            continue;
        }

        if (runStart >= 0 && x - runStart > best.width)
        {
            best.width = x - runStart;
            best.start = runStart;
        }

        runStart = -1;
    }

    return best;
}

int FirstTallColumn(const std::vector<int>& ink, int tall, int from, int to)
{
    const int end = (std::min)(static_cast<int>(ink.size()) - 1, to);

    for (int x = (std::max)(0, from); x <= end; ++x)
    {
        if (ink[x] >= tall)
            return x;
    }

    return -1;
}

std::vector<TileGap> FindTileGaps(const std::vector<int>& ink, int tall, int lastInk)
{
    const int width = static_cast<int>(ink.size());
    std::vector<TileGap> gaps;

    for (int x = 0; x <= lastInk;)
    {
        if (ink[x] != 0)
        {
            ++x;
            continue;
        }

        const int blankStart = x;

        while (x <= lastInk && ink[x] == 0)
            ++x;

        const int blankEnd = x - 1;

        if ((blankEnd - blankStart + 1) * 100 > width * kMaxTileGapPercent)
            continue;

        const int frame = FirstTallColumn(ink, tall, blankEnd + 1, blankEnd + 1 + kTileEdge);

        if (frame < 0)
            continue;

        const bool closed = FirstTallColumn(ink, tall, blankStart - 1 - kTileEdge, blankStart - 1) >= 0;
        gaps.push_back({ blankStart, blankEnd, frame, closed });
    }

    return gaps;
}

int AnchorTileRight(const std::vector<int>& ink, int tall, const TileGap& first, const TileGap& second)
{
    int tileRight = second.blankStart - 1;

    while (tileRight > first.frame && ink[tileRight] < tall)
        --tileRight;

    return tileRight;
}

TileChain FollowTileGrid(const std::vector<TileGap>& gaps, const TileGap& second, int pitch, int blank)
{
    TileChain chain{ &second, 2 };

    for (;;)
    {
        const int next = chain.end->frame + pitch;
        const TileGap* follow = nullptr;

        for (const TileGap& gap : gaps)
        {
            if (gap.blankStart <= chain.end->frame || gap.Blank() > blank + 1 || std::abs(gap.frame - next) > kTileSlack + 1)
                continue;

            if (!follow || std::abs(gap.frame - next) < std::abs(follow->frame - next))
                follow = &gap;
        }

        if (!follow)
            return chain;

        chain.end = follow;
        ++chain.count;
    }
}

TileGrid FindTileGrid(const std::vector<int>& ink, int tall, int lastInk)
{
    const int width = static_cast<int>(ink.size());
    const std::vector<TileGap> gaps = FindTileGaps(ink, tall, lastInk);

    TileGrid best;

    for (std::size_t i = 0; i < gaps.size(); ++i)
    {
        const TileGap& first = gaps[i];

        if (!first.closed)
            continue;

        if (first.blankStart * 100 > width * kFirstTileGapPercent)
            break;

        for (std::size_t j = i + 1; j < gaps.size(); ++j)
        {
            const TileGap& second = gaps[j];

            if (!second.closed)
                continue;

            const int pitch = second.frame - first.frame;

            if (pitch * 100 < width * kMinTilePitchPercent)
                continue;

            if (pitch * 100 > width * kMaxTilePitchPercent)
                break;

            if (std::abs(second.Blank() - first.Blank()) > 1 || first.blankStart < pitch - first.Blank())
                continue;

            const int tileRight = AnchorTileRight(ink, tall, first, second);

            if (tileRight <= first.frame)
                continue;

            const TileChain chain = FollowTileGrid(gaps, second, pitch, first.Blank());

            if (chain.count > best.count)
                best = { chain.count, chain.end->frame + tileRight - first.frame, first.Blank() };
        }
    }

    return best;
}

LastTile FindLastTile(const std::vector<int>& ink, int tall, int lastInk)
{
    const TileGrid grid = FindTileGrid(ink, tall, lastInk);

    if (grid.count == 0)
        return {};

    const int expected = grid.lastRight;
    int right = -1;

    for (int x = expected - kTileSlack; x <= expected + kTileSlack; ++x)
    {
        if (x >= 0 && x <= lastInk && ink[x] >= tall)
            right = x;
    }

    if (right < 0)
    {
        right = (std::min)(expected, lastInk);

        while (right > expected - kTileSlack && ink[right] == 0)
            --right;

        if (ink[right] == 0)
            return {};
    }

    return { right, grid.blank };
}

int ChooseTextStart(const std::vector<int>& ink, int height, int lastInk, const Gap& gap)
{
    const int inGap = gap.start + static_cast<int>(gap.width * kGapBias);
    const int tall = static_cast<int>(std::ceil(height * kTileFrameInk));
    const LastTile tile = FindLastTile(ink, tall, lastInk);

    if (tile.right < 0 || tile.right >= lastInk)
        return inGap;

    const int solid = (std::max)(2, static_cast<int>(std::ceil(height * kTextInk)));

    int textFrom = tile.right + 1 + kTileEdge;

    while (textFrom <= lastInk && ink[textFrom] < solid)
        ++textFrom;

    const int afterTile = tile.right + 1 + static_cast<int>((textFrom - tile.right - 1 - kTileEdge) * kGapBias);

    if (gap.start <= tile.right)
        return afterTile;

    if (gap.start < textFrom)
        return inGap;

    const int nextFrame = (std::min)(lastInk, tile.right + tile.blank + kTileEdge + 1);

    if (FirstTallColumn(ink, tall, tile.right + kTileEdge, nextFrame) >= 0 || gap.start - textFrom < tile.blank)
        return inGap;

    int covered = 0;

    for (int x = textFrom; x < gap.start; ++x)
        covered += ink[x];

    const double coverage = static_cast<double>(covered) / (static_cast<double>(height) * (gap.start - textFrom));

    return coverage < kMinTextCoverage ? inGap : afterTile;
}

int RowTextStart(const cv::Mat& rowGray)
{
    cv::Mat dark;
    cv::threshold(rowGray, dark, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    const std::vector<int> ink = ColumnInk(dark);
    const int lastInk = LastInkColumn(ink);
    const Gap gap = WidestGap(ink, lastInk);

    if (gap.start < 0)
        return -1;

    return ChooseTextStart(ink, dark.rows, lastInk, gap);
}
}

std::vector<int> FindTextStartX(const cv::Mat& gray, const std::vector<cv::Rect>& rows)
{
    std::vector<int> starts(rows.size(), 0);

    if (gray.empty() || rows.empty())
        return starts;

    std::vector<int> known;

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        starts[i] = RowTextStart(gray(rows[i]));

        if (starts[i] >= 0)
            known.push_back(starts[i]);
    }

    int fallback = 0;

    if (!known.empty())
    {
        std::sort(known.begin(), known.end());
        fallback = known[known.size() / 2];
    }

    for (int& value : starts)
    {
        if (value < 0)
            value = fallback;
    }

    return starts;
}
