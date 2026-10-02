#include "items/MapItem.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <utility>

#include "common/Text.h"
#include "items/ItemText.h"

namespace
{
bool IsDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

std::string RemoveRollRanges(std::string_view text)
{
    std::string result;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '(' && i > 0 && IsDigit(text[i - 1]))
        {
            const auto end = text.find(')', i);

            if (end != std::string_view::npos)
            {
                const auto range = text.substr(i + 1, end - i - 1);

                if (!range.empty() &&
                    std::all_of(
                        range.begin(),
                        range.end(),
                        [](char ch) { return IsDigit(ch) || ch == '-' || ch == '+' || ch == '.' || ch == ',' || ch == ' '; }
                    ))
                {
                    i = end;
                    continue;
                }
            }
        }

        result += text[i];
    }

    return result;
}

std::string ModifierPattern(std::string_view text)
{
    std::string result;
    bool number = false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        const bool separator = number && (ch == '.' || ch == ',') && i + 1 < text.size() && IsDigit(text[i + 1]);

        if (IsDigit(ch) || separator)
        {
            if (!number)
                result += '#';

            number = true;
        }
        else
        {
            number = false;
            result += ch;
        }
    }

    return result;
}

std::pair<double, bool> FirstNumericValue(std::string_view text)
{
    for (std::size_t start = 0; start < text.size(); ++start)
    {
        if (!IsDigit(text[start]) && !(text[start] == '+' && start + 1 < text.size() && IsDigit(text[start + 1])) &&
            !(text[start] == '-' && start + 1 < text.size() && IsDigit(text[start + 1])))
            continue;

        std::size_t end = start;

        if (text[end] == '+' || text[end] == '-')
            ++end;

        while (end < text.size() && IsDigit(text[end]))
            ++end;

        if (end + 1 < text.size() && (text[end] == '.' || text[end] == ',') && IsDigit(text[end + 1]))
        {
            ++end;

            while (end < text.size() && IsDigit(text[end]))
                ++end;
        }

        std::string number(text.substr(start, end - start));

        if (number.starts_with('+'))
            number.erase(number.begin());

        std::replace(number.begin(), number.end(), ',', '.');
        double value = 0.0;
        const auto [ptr, error] = std::from_chars(number.data(), number.data() + number.size(), value);

        if (error == std::errc{} && ptr == number.data() + number.size())
            return { value, true };

        return { 0.0, false };
    }

    return { 0.0, false };
}

std::string CleanProperty(std::string_view text)
{
    std::string clean(text);

    for (const auto suffix : { " (implicit)", " (enchant)", " (crafted)", " (augmented)", " (fractured)" })
    {
        if (clean.ends_with(suffix))
            clean.erase(clean.size() - std::string_view(suffix).size());
    }

    return clean;
}

bool IsDescription(std::string_view line, const ItemLanguage& language)
{
    return ToLowerAscii(line).find(ToLowerAscii(language.mapDevice)) != std::string_view::npos || line.starts_with('"') ||
           line.starts_with("«");
}

bool IsUsesRemaining(std::string_view line, const ItemLanguage& language)
{
    return line.find(language.uses) != std::string_view::npos && std::any_of(line.begin(), line.end(), IsDigit);
}
}

std::optional<MapItem> ParseMapItem(const ItemText& parsed)
{
    if (!parsed.language)
        return std::nullopt;

    const auto& language = *parsed.language;

    if (parsed.itemClass != language.waystones && parsed.itemClass != language.tablets && parsed.itemClass != language.tabletAlias)
        return std::nullopt;

    MapItem item;
    item.name = parsed.name;
    item.base = parsed.base;
    item.rarity = parsed.rarity;
    item.corrupted = parsed.corrupted;
    bool itemLevel = false;

    for (const auto& section : parsed.sections)
    {
        std::string details;

        for (const std::string_view line : section)
        {
            if (line == language.corrupted || line == language.twiceCorrupted)
                continue;

            if (!ItemPropertyValue(line, language.itemLevel).empty())
            {
                itemLevel = true;
                const std::string clean = CleanProperty(line);
                const auto [value, hasValue] = FirstNumericValue(clean);
                item.properties.push_back({ std::string(line), ModifierPattern(clean), value, hasValue });
                continue;
            }

            if (line.starts_with('{') && line.ends_with('}'))
            {
                details = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            if (IsDescription(line, language))
                break;

            if (IsUsesRemaining(line, language) || (!itemLevel && details.empty()))
            {
                const std::string clean = CleanProperty(line);
                const auto [value, hasValue] = FirstNumericValue(clean);
                item.properties.push_back({ std::string(line), ModifierPattern(clean), value, hasValue });
                continue;
            }

            std::string clean = RemoveRollRanges(line);

            for (const auto suffix : { " (implicit)", " (enchant)", " (crafted)", " (augmented)", " (fractured)" })
            {
                if (clean.ends_with(suffix))
                    clean.erase(clean.size() - std::string_view(suffix).size());
            }

            MapModifier modifier;
            modifier.pattern = ModifierPattern(clean);
            const auto [value, hasValue] = FirstNumericValue(clean);
            modifier.value = value;
            modifier.hasValue = hasValue;
            modifier.text = std::move(clean);
            modifier.details = details;
            item.modifiers.push_back(std::move(modifier));
        }
    }

    if (!itemLevel)
        return std::nullopt;

    return item;
}
