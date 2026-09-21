#include "LootParser.h"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <string>

namespace
{
char DigitFromOcr(char c)
{
    switch (c)
    {
    case 'i':
    case 'I':
    case 'l':
    case '|':
    case '!':
        return '1';
    case 'O':
    case 'o':
        return '0';
    case 'S':
        return '5';
    default:
        return std::isdigit(static_cast<unsigned char>(c)) ? c : '\0';
    }
}
}

LootParser::ParsedLootLineStruct LootParser::ParseLootLine(const std::string& line)
{
    size_t pos = 0;
    while (pos < line.size() && std::isspace((unsigned char)line[pos]))
        ++pos;

    std::string digits;

    while (pos < line.size())
    {
        const char digit = DigitFromOcr(line[pos]);

        if (digit == '\0')
            break;

        digits.push_back(digit);
        ++pos;
    }

    if (!digits.empty() && pos < line.size() && (line[pos] == 'x' || line[pos] == 'X'))
    {
        ++pos;

        while (pos < line.size() && std::isspace((unsigned char)line[pos]))
            ++pos;

        int quantity = 1;

        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), quantity);

        if (parsed.ec != std::errc{} || quantity <= 0)
            quantity = 1;

        return { quantity, line.substr(pos) };
    }

    return { 1, line };
}

std::string LootParser::FormatAmount(double value, const std::string& unit)
{
    int decimals = 2;

    if (value >= 100.0)
    {
        decimals = 0;
    }
    else if (value >= 10.0)
    {
        decimals = 1;
    }
    else if (value > 0.0 && value < 1.0)
    {
        for (double scaled = value * 100.0; scaled < 1.0 && decimals < 8; scaled *= 10.0)
            ++decimals;
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);

    std::string s = buf;

    if (s.find('.') != std::string::npos)
    {
        while (!s.empty() && s.back() == '0')
            s.pop_back();

        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }

    return s + " " + unit;
}

std::string LootParser::FormatPrice(double value)
{
    return FormatAmount(value, "ex");
}

std::string LootParser::FormatDivine(double divines)
{
    return FormatAmount(divines, "div");
}

std::string LootParser::FormatStack(double unitValue, int quantity, const std::string& unit)
{
    const std::string single = FormatAmount(unitValue, unit);

    if (quantity <= 1)
        return single;

    return single + " (" + FormatAmount(unitValue * quantity, unit) + ")";
}
