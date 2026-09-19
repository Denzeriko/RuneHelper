#include "LootParser.h"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <optional>
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
        std::snprintf(buf, sizeof(buf), "%.1f ex", value);
    else
        std::snprintf(buf, sizeof(buf), "%.2f ex", value);

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
