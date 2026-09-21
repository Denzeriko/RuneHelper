#pragma once

#include <optional>
#include <string>

struct ResolvedPrice
{
    std::string name;
    std::optional<double> unitEx;
    int confidence = 0;
    double totalEx = 0.0;
};
