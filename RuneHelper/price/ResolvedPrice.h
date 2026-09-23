#pragma once

#include <optional>
#include <string>

constexpr int kTrustedMatchConfidence = 85;

struct ResolvedPrice
{
    std::string name;
    std::optional<double> unitEx;
    int confidence = 0;
    double totalEx = 0.0;
};
