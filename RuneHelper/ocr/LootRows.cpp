#include "ocr/LootRows.h"

#include <algorithm>

#include "ocr/LootParser.h"

std::vector<FrameRow> ParseLootRows(
    const std::vector<LootLine>& loot,
    const cv::Rect& region,
    const AppConfig& config,
    const CachedItemNames* translations
)
{
    std::vector<FrameRow> rows;
    rows.reserve(loot.size());

    for (const auto& item : loot)
    {
        const auto parsed = LootParser::ParseLootLine(item.text);

        FrameRow row;
        row.name = parsed.itemName;
        row.quantity = parsed.quantity;

        if (translations)
        {
            if (const auto english = translations->FindBest(row.name))
                row.name = english->name;
        }

        row.textTop = item.y1;
        row.overlayY = region.y + (item.y1 + item.y2) / 2 + config.overlayOffsetY;

        rows.push_back(std::move(row));
    }

    return rows;
}

int OverlayTextX(const cv::Rect& region, const std::optional<cv::Rect>& panel, const AppConfig& config)
{
    const int right = panel && !panel->empty() ? std::min(panel->x + panel->width, region.width) : region.width;

    return region.x + right + config.overlayOffsetX;
}
