#pragma once

#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "core/Config.h"
#include "ocr/NameMatcher.h"
#include "ocr/OCR.h"
#include "price/ResolvedPrice.h"

struct FrameRow
{
    std::string name;
    int quantity = 1;
    int textTop = 0;
    int overlayY = 0;
    bool missingPrice = false;
    ResolvedPrice price;
};

std::vector<FrameRow> ParseLootRows(
    const std::vector<LootLine>& loot,
    const cv::Rect& region,
    const AppConfig& config,
    const NameMatcher* translations = nullptr
);
int OverlayTextX(const cv::Rect& region, const std::optional<cv::Rect>& panel, const AppConfig& config);
