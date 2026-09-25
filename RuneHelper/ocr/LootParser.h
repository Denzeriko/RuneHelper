#pragma once

#include <string>

namespace LootParser
{
struct ParsedLootLineStruct
{
    int quantity = 1;
    std::string itemName;
};

ParsedLootLineStruct ParseLootLine(const std::string& line);
std::string FormatAmount(double value, const std::string& unit);
std::string FormatPrice(double value);
std::string FormatStack(double unitValue, int quantity, const std::string& unit);
}