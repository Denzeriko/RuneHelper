#include "LootParser.h"

#include <regex>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <optional>

LootParser::ParsedLootLineStruct LootParser::ParseLootLine(const std::string& line)
{
    static const std::regex re(R"(^\s*(\d+)x\s+(.+?)\s*$)");

    std::smatch m;

    if (std::regex_match(line, m, re))
    {
        ParsedLootLineStruct result;
        result.quantity = std::stoi(m[1].str());
        result.itemName = m[2].str();
        return result;
    }

    return ParsedLootLineStruct{
        1,
        line
    };
}

std::optional<double> LootParser::ParsePriceValue(const std::string& price)
{
    static const std::regex re(R"(([0-9]+(?:\.[0-9]+)?))");

    std::smatch m;

    if (std::regex_search(price, m, re))
        return std::stod(m[1].str());

    return std::nullopt;
}

std::string LootParser::FormatPrice(double value)
{
    std::ostringstream ss;

    if (value >= 10.0)
        ss << std::fixed << std::setprecision(1);
    else
        ss << std::fixed << std::setprecision(2);

    ss << value << " ex";

    std::string s = ss.str();

    // 1.50 -> 1.5, 1.00 -> 1
    while (s.find('.') != std::string::npos &&
        s.find(" ex") != std::string::npos &&
        s[s.find(" ex") - 1] == '0')
    {
        s.erase(s.find(" ex") - 1, 1);
    }

    if (s.find(". ex") != std::string::npos)
        s.erase(s.find(". ex"), 1);

    return s;
}

std::string LootParser::FormatStackPrice(const std::string& singlePrice, int quantity)
{
    auto value = ParsePriceValue(singlePrice);

    if (!value || quantity <= 1)
        return singlePrice;

    double total = *value * quantity;

    return singlePrice + " (" + FormatPrice(total) + ")";
}