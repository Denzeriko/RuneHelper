#pragma once

#include <opencv2/core.hpp>

#include "ui/OverlayState.h"

namespace OverlayRenderer
{
cv::Rect ContentBounds(const OverlayState& state);

void Paint(cv::Mat& canvas, const cv::Point& origin, const OverlayState& state);
}
