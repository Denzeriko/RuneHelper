#include "price/PriceColors.h"

#include <limits>

namespace
{
constexpr double kAutoMediumShare = 0.05;
constexpr double kAutoHighShare = 0.2;
constexpr double kAutoVeryHighShare = 0.5;

constexpr OverlayColor kLowColor = OverlayRgb(160, 160, 160);
constexpr OverlayColor kMediumColor = OverlayRgb(80, 255, 80);
constexpr OverlayColor kHighColor = OverlayRgb(255, 220, 80);
constexpr OverlayColor kVeryHighColor = OverlayRgb(255, 60, 60);
}

PriceTiers ManualPriceTiers(const AppConfig& config)
{
    return {
        static_cast<double>(config.priceColorMedium),
        static_cast<double>(config.priceColorHigh),
        static_cast<double>(config.priceColorVeryHigh),
    };
}

PriceTiers AutoPriceTiers(double bestTotalEx)
{
    if (bestTotalEx <= 0.0)
    {
        constexpr double kNever = std::numeric_limits<double>::infinity();
        return { kNever, kNever, kNever };
    }

    return { bestTotalEx * kAutoMediumShare, bestTotalEx * kAutoHighShare, bestTotalEx * kAutoVeryHighShare };
}

OverlayColor PriceColor(double totalEx, const PriceTiers& tiers)
{
    if (totalEx >= tiers.veryHigh)
        return kVeryHighColor;

    if (totalEx >= tiers.high)
        return kHighColor;

    if (totalEx >= tiers.medium)
        return kMediumColor;

    return kLowColor;
}
