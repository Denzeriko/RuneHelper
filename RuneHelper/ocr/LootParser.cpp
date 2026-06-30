#include "LootParser.h"

#include <regex>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <optional>

LootParser::ParsedLootLineStruct LootParser::ParseLootLine(const std::string& line)
{
    size_t pos = 0;
    while (pos < line.size() && std::isspace((unsigned char)line[pos]))
        ++pos;

    size_t numStart = pos;
    while (pos < line.size() && std::isdigit((unsigned char)line[pos]))
        ++pos;

    if (pos > numStart && pos < line.size() && line[pos] == 'x')
    {
        ++pos;

        while (pos < line.size() && std::isspace((unsigned char)line[pos]))
            ++pos;

        int quantity = std::stoi(line.substr(numStart, pos - numStart));

        return { quantity, line.substr(pos) };
    }

    return { 1, line };
}

std::optional<double> LootParser::ParsePriceValue(const std::string& price)
{
    const char* s = price.c_str();
    char* end = nullptr;
    double value = std::strtod(s, &end);

    if (end == s)
        return std::nullopt;

    return value;
}

std::string LootParser::FormatPrice(double value)
{
    char buf[64];

    if (value >= 10.0)
        sprintf_s(buf, "%.1f ex", value);
    else
        sprintf_s(buf, "%.2f ex", value);

    std::string s = buf;

    while (s.find('.') != std::string::npos &&
        s.find(" ex") != std::string::npos &&
        s[s.find(" ex") - 1] == '0')
    {
        s.erase(s.find(" ex") - 1, 1);
    }

    size_t dotEx = s.find(". ex");
    if (dotEx != std::string::npos)
        s.erase(dotEx, 1);

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