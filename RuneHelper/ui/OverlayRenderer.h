#pragma once

#include <opencv2/core.hpp>

#include "ui/OverlayState.h"

namespace OverlayRenderer
{
cv::Rect ContentBounds(const OverlayState& state);
int PanelContentWidth(const OverlayPanel& panel, float scale = 1.0f);
int PanelContentHeight(const OverlayPanel& panel, float scale = 1.0f);

void Paint(cv::Mat& canvas, const cv::Point& origin, const OverlayState& state);
}
