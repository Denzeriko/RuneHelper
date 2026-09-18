#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "core/Config.h"
#include "ocr/OCR.h"

struct FrameRow
{
    std::string name;
    int quantity = 1;
    int textTop = 0;
    int overlayY = 0;
};

std::vector<FrameRow> ParseLootRows(
    const std::vector<LootLine>& loot,
    const cv::Rect& region,
    const AppConfig& config);
