#include "features/PriceOverlayFeature.h"

#include <string>

#include "ocr/LootParser.h"

namespace
{
OverlayColor ColorForPrice(double priceEx, const AppConfig& config)
{
    if (priceEx >= config.priceColorVeryHigh)
        return OverlayRgb(255, 60, 60);

    if (priceEx >= config.priceColorHigh)
        return OverlayRgb(255, 220, 80);

    if (priceEx >= config.priceColorMedium)
        return OverlayRgb(80, 255, 80);

    return OverlayRgb(160, 160, 160);
}
}

void PriceOverlayFeature::OnFrame(FrameContext& frame)
{
    if (!frame.config.priceSearchEnabled)
        return;

    for (size_t i = 0; i < frame.rows.size(); ++i)
    {
        const FrameRow& row = frame.rows[i];
        const ResolvedPrice& resolved = row.price;

        if (!resolved.unitEx)
            continue;

        if (i < frame.debug.lines.size())
        {
            frame.debug.lines[i].matchedText = resolved.name;
            frame.debug.lines[i].price = LootParser::FormatPrice(*resolved.unitEx);
            frame.debug.lines[i].priceEx = *resolved.unitEx;
            frame.debug.lines[i].confidence = resolved.confidence;
        }

        const bool hasRate = frame.divineToEx > 0.0;
        const bool inDivine = hasRate && frame.config.priceUnit == PriceUnit::Divine;

        std::string note = inDivine ? LootParser::FormatStack(*resolved.unitEx / frame.divineToEx, row.quantity, "div")
                                    : LootParser::FormatStack(*resolved.unitEx, row.quantity, "ex");

        if (resolved.confidence < kTrustedMatchConfidence)
            note += " ?";

        frame.rowOverlays[i].Append(note);

        if (hasRate && frame.config.priceUnit == PriceUnit::ExaltedWithDivine)
        {
            const double divines = resolved.totalEx / frame.divineToEx;

            if (divines >= 1.0)
                frame.rowOverlays[i].Append(LootParser::FormatDivine(divines));
        }

        frame.rowOverlays[i].SetColor(ColorForPrice(resolved.totalEx, frame.config));
    }
}
