#pragma once

#include <opencv2/opencv.hpp>

class RegionSelector
{
public:
    RegionSelector() = default;
    ~RegionSelector();

    RegionSelector(const RegionSelector&) = delete;
    RegionSelector& operator=(const RegionSelector&) = delete;

    cv::Rect Select();
};
