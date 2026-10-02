#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ItemLanguage
{
    std::string_view code;
    std::string_view itemClass;
    std::string_view waystones;
    std::string_view tablets;
    std::string_view rarity;
    std::string_view itemLevel;
    std::string_view corrupted;
    std::string_view twiceCorrupted;
    std::string_view mapDevice;
    std::string_view uses;
    std::string_view tabletAlias{};
};

struct ItemText
{
    const ItemLanguage* language = nullptr;
    std::string itemClass;
    std::string name;
    std::string base;
    std::string rarity;
    std::vector<std::vector<std::string>> sections;
    bool corrupted = false;
};

struct CopiedItem
{
    std::string text;
    std::optional<ItemText> item;
};

std::optional<ItemText> ParseItemText(std::string_view text);
std::string_view ItemPropertyValue(std::string_view line, std::string_view label);
