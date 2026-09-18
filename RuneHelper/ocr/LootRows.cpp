#include "ocr/LootRows.h"

#include "ocr/LootParser.h"

std::vector<FrameRow> ParseLootRows(
    const std::vector<LootLine>& loot,
    const cv::Rect& region,
    const AppConfig& config)
{
    std::vector<FrameRow> rows;
    rows.reserve(loot.size());

    for (const auto& item : loot)
    {
        const auto parsed = LootParser::ParseLootLine(item.text);

        FrameRow row;
        row.name = parsed.itemName;
        row.quantity = parsed.quantity;
        row.textTop = item.y1;
        row.overlayY = region.y + (item.y1 + item.y2) / 2 + config.overlayOffsetY;

        rows.push_back(std::move(row));
    }

    return rows;
}
