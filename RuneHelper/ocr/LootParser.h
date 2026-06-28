#pragma once

#include <optional>
#include <string>

namespace LootParser
{
    struct ParsedLootLineStruct
    {
        int quantity = 1;
        std::string itemName;
    };

    ParsedLootLineStruct ParseLootLine(const std::string& line);

    std::optional<double> ParsePriceValue(const std::string& price);

    std::string FormatPrice(double value);

    std::string FormatStackPrice(const std::string& singlePrice, int quantity);
}