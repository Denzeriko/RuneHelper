#include "ocr/LootOverlayBuilder.h"

#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "ocr/LootParser.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
std::wstring ToWide(const std::string& s)
{
    if (s.empty())
        return L"";

#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);

    if (size <= 0)
        return L"";

    std::wstring result(size - 1, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);

    return result;
#else
    return std::wstring(s.begin(), s.end());
#endif
}

OverlayColor OverlayColorForPrice(double priceEx, const AppConfig& config)
{
    if (priceEx > config.priceColorVeryHigh)
        return OverlayRgb(255, 60, 60);

    if (priceEx > config.priceColorHigh)
        return OverlayRgb(255, 220, 80);

    if (priceEx > config.priceColorMedium)
        return OverlayRgb(80, 255, 80);

    return OverlayRgb(160, 160, 160);
}
}

LootOverlayBuildResult LootOverlayBuilder::Build(
    const std::vector<LootLine>& loot,
    const cv::Rect& region,
    const AppConfig& config,
    PriceCache& priceCache,
    const CachedItemNames& cachedNames)
{
    LootOverlayBuildResult result;

    for (const auto& item : loot)
    {
        DebugLine debugLine;
        debugLine.ocrText = item.text;
        debugLine.matchedText = "-";
        debugLine.price = "-";
        debugLine.confidence = 0;

        auto parsed = LootParser::ParseLootLine(item.text);

        std::string rawName = parsed.itemName;
        int quantity = parsed.quantity;

        if (!config.priceSearchEnabled)
        {
            result.debug.lines.push_back(std::move(debugLine));
            continue;
        }

        auto price = priceCache.GetPrice(rawName);

        if (price)
        {
            debugLine.matchedText = rawName;
            debugLine.confidence = 100;
        }
        else
        {
            auto guess = cachedNames.FindBest(rawName);

            if (guess)
            {
                debugLine.matchedText = guess->name;
                debugLine.confidence = guess->confidence;
                price = priceCache.GetPrice(guess->name);
            }
        }

        if (!price)
        {
            result.debug.lines.push_back(std::move(debugLine));
            continue;
        }

        debugLine.price = *price;

        std::optional<double> value = LootParser::ParsePriceValue(*price);
        double totalValue = value ? (*value * quantity) : 0.0;

        int overlayY = region.y + (item.y1 + item.y2) / 2 + config.overlayOffsetY;

        if (HasCloseOverlayText(result.texts, overlayY, 25))
        {
            result.debug.lines.push_back(std::move(debugLine));
            continue;
        }

        OverlayText t;
        t.color = OverlayColorForPrice(totalValue, config);
        t.text = ToWide(LootParser::FormatStackPrice(*price, quantity));
        t.x = region.x + region.width + config.overlayOffsetX;
        t.y = region.y + (item.y1 + item.y2) / 2 + config.overlayOffsetY;

        result.texts.push_back(std::move(t));
        result.debug.lines.push_back(std::move(debugLine));
    }

    return result;
}

bool LootOverlayBuilder::HasCloseOverlayText(const std::vector<OverlayText>& texts, int y, int minDistance)
{
    for (const auto& t : texts)
    {
        if (std::abs(t.y - y) < minDistance)
            return true;
    }

    return false;
}
