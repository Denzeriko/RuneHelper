#pragma once

#include <optional>
#include <string>

struct ResolvedPrice
{
    std::string name;
    std::optional<std::string> price;
    int confidence = 0;
    double value = 0.0;
};
