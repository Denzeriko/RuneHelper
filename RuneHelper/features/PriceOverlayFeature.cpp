#include "features/PriceOverlayFeature.h"

#include <algorithm>
#include <string>
#include <vector>

#include "ocr/LootParser.h"
#include "price/PriceColors.h"
#include "ui/OverlayIcons.h"

namespace
{
constexpr const char* kNoPriceNote = "no price";

double BestTotalEx(const std::vector<FrameRow>& rows)
{
    double best = 0.0;

    for (const FrameRow& row : rows)
    {
        if (row.price.unitEx)
            best = std::max(best, row.price.totalEx);
    }

    return best;
}

std::string UnitText(const OverlayIcon& icon, bool picture)
{
    return picture ? IconString(icon) : std::string(icon.label);
}
}

void PriceOverlayFeature::OnFrame(FrameContext& frame)
{
    if (!frame.config.priceSearchEnabled)
        return;

    const std::string exalted = UnitText(kExaltedOrbIcon, frame.config.overlayIcons);
    const std::string divine = UnitText(kDivineOrbIcon, frame.config.overlayIcons);
    const PriceTiers tiers = frame.config.autoPriceColors ? AutoPriceTiers(BestTotalEx(frame.rows)) : ManualPriceTiers(frame.config);

    for (size_t i = 0; i < frame.rows.size(); ++i)
    {
        const FrameRow& row = frame.rows[i];
        const ResolvedPrice& resolved = row.price;

        if (!resolved.unitEx)
        {
            if (row.missingPrice)
                frame.rowOverlays[i].Append(kNoPriceNote);

            continue;
        }

        if (i < frame.debug.lines.size())
        {
            frame.debug.lines[i].matchedText = resolved.name;
            frame.debug.lines[i].price = LootParser::FormatPrice(*resolved.unitEx);
            frame.debug.lines[i].priceEx = *resolved.unitEx;
            frame.debug.lines[i].confidence = resolved.confidence;
        }

        const bool hasRate = frame.divineToEx > 0.0;
        const bool inDivine = hasRate && frame.config.priceUnit == PriceUnit::Divine;

        std::string note = inDivine ? LootParser::FormatStack(*resolved.unitEx / frame.divineToEx, row.quantity, divine)
                                    : LootParser::FormatStack(*resolved.unitEx, row.quantity, exalted);

        if (resolved.confidence < kTrustedMatchConfidence)
            note += " ?";

        frame.rowOverlays[i].Append(note);

        if (hasRate && frame.config.priceUnit == PriceUnit::ExaltedWithDivine)
        {
            const double divines = resolved.totalEx / frame.divineToEx;

            if (divines >= 1.0)
                frame.rowOverlays[i].Append(LootParser::FormatAmount(divines, divine));
        }

        frame.rowOverlays[i].SetColor(PriceColor(resolved.totalEx, tiers));
    }
}
