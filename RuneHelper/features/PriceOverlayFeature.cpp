#include "features/PriceOverlayFeature.h"

#include <string>

#include "ocr/LootParser.h"
#include "price/PriceService.h"

namespace
{
constexpr int kTrustedMatchConfidence = 85;

OverlayColor ColorForPrice(double priceEx, const AppConfig& config)
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

void PriceOverlayFeature::OnFrame(FrameContext& frame)
{
    if (!frame.prices || !frame.config.priceSearchEnabled)
        return;

    for (size_t i = 0; i < frame.rows.size(); ++i)
    {
        const FrameRow& row = frame.rows[i];
        const ResolvedPrice resolved = frame.prices->Resolve(row.name, row.quantity);

        if (i < frame.debug.lines.size() && resolved.price)
        {
            frame.debug.lines[i].matchedText = resolved.name;
            frame.debug.lines[i].price = *resolved.price;
            frame.debug.lines[i].confidence = resolved.confidence;
        }

        if (!resolved.price)
            continue;

        std::string note = LootParser::FormatStackPrice(*resolved.price, row.quantity);

        if (resolved.confidence < kTrustedMatchConfidence)
            note += " ?";

        frame.rowOverlays[i].Append(note);
        frame.rowOverlays[i].SetColor(ColorForPrice(resolved.value, frame.config));
    }
}
