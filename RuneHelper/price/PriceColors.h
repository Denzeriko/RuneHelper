#pragma once

#include "core/Config.h"
#include "ui/OverlayState.h"

struct PriceTiers
{
    double medium = 0.0;
    double high = 0.0;
    double veryHigh = 0.0;
};

PriceTiers ManualPriceTiers(const AppConfig& config);
PriceTiers AutoPriceTiers(double bestTotalEx);
OverlayColor PriceColor(double totalEx, const PriceTiers& tiers);
