#include "LootParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view kCyrillicSmallHa = "\xD1\x85";
constexpr std::string_view kCyrillicCapitalHa = "\xD0\xA5";
constexpr std::size_t kMaxQuantityDigits = 3;

std::size_t CodePoints(std::string_view text)
{
    std::size_t count = 0;

    for (unsigned char ch : text)
    {
        if ((ch & 0xC0) != 0x80)
            ++count;
    }

    return count;
}

bool CanBeWholeWord(std::string_view character)
{
    if (character.size() != 3)
        return false;

    const char32_t c = ((static_cast<unsigned char>(character[0]) & 0x0Fu) << 12) |
                       ((static_cast<unsigned char>(character[1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(character[2]) & 0x3Fu);

    return (c >= 0x0E00 && c <= 0x0E7F) || (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x9FFF) ||
           (c >= 0xAC00 && c <= 0xD7A3);
}

char DigitFromOcr(char c)
{
    switch (c)
    {
    case 'i':
    case 'I':
    case 'l':
    case '|':
    case '!': return '1';
    case 'O':
    case 'o': return '0';
    case 'S': return '5';
    default: return std::isdigit(static_cast<unsigned char>(c)) ? c : '\0';
    }
}

std::size_t TimesSignAt(const std::string& line, std::size_t pos, std::size_t digitCount)
{
    if (pos >= line.size())
        return 0;

    if (line[pos] == 'x' || line[pos] == 'X')
        return 1;

    const std::string_view rest = std::string_view(line).substr(pos);

    if (rest.starts_with(kCyrillicSmallHa) || rest.starts_with(kCyrillicCapitalHa))
        return kCyrillicSmallHa.size();

    const bool misreadTimes = line[pos] == 'n' || line[pos] == 'w';

    return digitCount == 1 && misreadTimes && pos + 1 < line.size() && line[pos + 1] == ' ' ? 1 : 0;
}

int OpenParens(std::string_view text)
{
    int open = 0;

    for (const char ch : text)
    {
        if (ch == '(')
            ++open;
        else if (ch == ')' && open > 0)
            --open;
    }

    return open;
}

bool TrailingQuantity(const std::string& line, int& quantity, std::size_t& nameEnd)
{
    std::size_t end = line.size();

    while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1])))
        --end;

    const bool closed = end > 0 && line[end - 1] == ')';

    if (closed)
        --end;

    std::string digits;
    bool realDigit = false;
    std::size_t begin = end;

    while (begin > 0 && digits.size() < kMaxQuantityDigits)
    {
        const char digit = DigitFromOcr(line[begin - 1]);

        if (digit == '\0')
            break;

        realDigit = realDigit || std::isdigit(static_cast<unsigned char>(line[begin - 1])) != 0;
        digits.insert(digits.begin(), digit);
        --begin;
    }

    const bool opened = begin > 0 && line[begin - 1] == '(';

    if (digits.empty() || !realDigit || (!opened && !closed))
        return false;

    const std::size_t start = opened ? begin - 1 : begin;

    if (start == 0)
        return false;

    if (!opened &&
        (!std::isspace(static_cast<unsigned char>(line[start - 1])) || OpenParens(std::string_view(line).substr(0, start)) > 0))
        return false;

    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), quantity);

    if (parsed.ec != std::errc{} || quantity <= 0)
        return false;

    nameEnd = start;
    return true;
}

bool TrailingTimesQuantity(const std::string& line, int& quantity, std::size_t& nameEnd)
{
    std::size_t end = line.size();

    while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1])))
        --end;

    std::string digits;
    std::size_t begin = end;

    while (begin > 0 && digits.size() < kMaxQuantityDigits)
    {
        const char digit = DigitFromOcr(line[begin - 1]);

        if (digit == '\0')
            break;

        digits.insert(digits.begin(), digit);
        --begin;
    }

    if (digits.empty() || begin < 2 || (line[begin - 1] != 'x' && line[begin - 1] != 'X'))
        return false;

    const std::size_t sign = begin - 1;

    if (!std::isspace(static_cast<unsigned char>(line[sign - 1])))
        return false;

    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), quantity);

    if (parsed.ec != std::errc{} || quantity <= 0)
        return false;

    nameEnd = sign;
    return true;
}

std::string StripTrailingNoise(std::string name)
{
    auto dropSpaces = [&name]
    {
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
    };

    dropSpaces();

    const std::size_t lastSpace = name.find_last_of(' ');

    if (lastSpace != std::string::npos)
    {
        const std::string_view last = std::string_view(name).substr(lastSpace + 1);

        if (CodePoints(last) == 1 && !CanBeWholeWord(last))
            name.erase(lastSpace);
    }

    while (!name.empty() && (name.back() == '-' || name.back() == '\''))
        name.pop_back();

    dropSpaces();

    return name;
}
}

LootParser::ParsedLootLineStruct LootParser::ParseLootLine(const std::string& line)
{
    size_t pos = 0;
    while (pos < line.size() && std::isspace((unsigned char)line[pos]))
        ++pos;

    const std::size_t digitsStart = pos;
    std::string digits;

    while (pos < line.size())
    {
        const char digit = DigitFromOcr(line[pos]);

        if (digit == '\0')
            break;

        digits.push_back(digit);
        ++pos;
    }

    const std::size_t timesSign = digits.empty() ? 0 : TimesSignAt(line, pos, digits.size());

    if (timesSign > 0)
    {
        pos += timesSign;

        while (pos < line.size() && std::isspace((unsigned char)line[pos]))
            ++pos;

        int quantity = 1;

        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), quantity);

        if (parsed.ec != std::errc{} || quantity <= 0)
            quantity = 1;

        return { quantity, StripTrailingNoise(line.substr(pos)) };
    }

    const bool plainDigits = !digits.empty() && digits.size() <= kMaxQuantityDigits &&
                             std::all_of(
                                 line.begin() + static_cast<std::ptrdiff_t>(digitsStart),
                                 line.begin() + static_cast<std::ptrdiff_t>(pos),
                                 [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }
                             );

    if (plainDigits && pos + 1 < line.size() && line[pos] == ' ' && !std::isdigit(static_cast<unsigned char>(line[pos + 1])))
    {
        int quantity = 1;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), quantity);

        if (parsed.ec == std::errc{} && quantity > 0)
            return { quantity, StripTrailingNoise(line.substr(pos + 1)) };
    }

    int quantity = 1;
    std::size_t nameEnd = 0;

    if (TrailingQuantity(line, quantity, nameEnd) || TrailingTimesQuantity(line, quantity, nameEnd))
        return { quantity, StripTrailingNoise(line.substr(0, nameEnd)) };

    return { 1, StripTrailingNoise(line) };
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
        double threshold = 0.01;

        while (value < threshold && decimals < 8)
        {
            threshold /= 10.0;
            ++decimals;
        }
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
    std::string single = FormatAmount(unitValue, unit);

    if (quantity <= 1)
        return single;

    return single + " (" + FormatAmount(unitValue * quantity, unit) + ")";
}
