#pragma once

#include <optional>
#include <string>
#include <vector>

#include "items/ItemText.h"

struct MapProperty
{
    std::string text;
    std::string pattern;
    double value = 0.0;
    bool hasValue = false;
};

struct MapModifier
{
    std::string text;
    std::string pattern;
    std::string details;
    double value = 0.0;
    bool hasValue = false;
};

struct MapItem
{
    std::string name;
    std::string base;
    std::string rarity;
    std::vector<MapProperty> properties;
    std::vector<MapModifier> modifiers;
    bool corrupted = false;
};

std::optional<MapItem> ParseMapItem(const ItemText& parsed);
