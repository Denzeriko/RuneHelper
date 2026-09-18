#pragma once

#include <vector>

#include <opencv2/core.hpp>

struct RuneTileBand
{
    int top = 0;
    int bottom = 0;
};

class RuneTileLocator
{
public:
    bool Analyze(const cv::Mat& gray);

    bool Valid() const { return valid_; }

    const RuneTileBand* BandFor(int y) const;
    cv::Rect TileRect(const RuneTileBand& band, int index) const;

private:
    bool valid_ = false;
    double x0_ = 0.0;
    double pitch_ = 0.0;
    int tileWidth_ = 0;
    std::vector<RuneTileBand> bands_;
};
