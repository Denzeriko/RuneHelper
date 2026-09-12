#pragma once

#include <vector>

#include <opencv2/core.hpp>

#include "core/Config.h"
#include "core/DebugData.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OCR.h"
#include "price/PriceCache.h"
#include "ui/OverlayState.h"

struct LootOverlayBuildResult
{
    DebugData debug;
    std::vector<OverlayText> texts;
};

class LootOverlayBuilder
{
public:
    static LootOverlayBuildResult Build(
        const std::vector<LootLine>& loot,
        const cv::Rect& region,
        const AppConfig& config,
        PriceCache& priceCache,
        const CachedItemNames& cachedNames);

private:
    static bool HasCloseOverlayText(const std::vector<OverlayText>& texts, int y, int minDistance);
};
