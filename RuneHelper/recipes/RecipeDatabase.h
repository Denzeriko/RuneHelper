#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "ocr/NameNormalizer.h"
#include "recipes/RecipeTypes.h"

std::filesystem::path DownloadedRecipeDatabasePath();

struct RecipeOutputKeyHash
{
    std::size_t operator()(const std::pair<std::string, int>& key) const
    {
        const std::size_t name = std::hash<std::string>{}(key.first);

        return name ^ (std::hash<int>{}(key.second) + 0x9e3779b97f4a7c15ULL + (name << 6) + (name >> 2));
    }
};

class RecipeDatabase
{
public:
    bool Load();
    bool LoadFromFile(const std::filesystem::path& path);

    bool Loaded() const { return loaded_; }
    bool Complete() const { return complete_; }
    const std::string& LoadedFrom() const { return loadedFrom_; }

    const std::vector<Recipe>& Recipes() const { return recipes_; }
    const Recipe* FindRecipe(std::string_view output, int count) const;
    bool IsRareRune(const std::string& rune) const;

private:
    static std::string NormalizeRune(std::string_view name);
    static std::string StripOcrNoise(std::string_view name);

    bool LoadFromJson(const nlohmann::json& j, const std::filesystem::path& path);

    bool loaded_ = false;
    bool complete_ = false;
    std::string loadedFrom_;
    std::vector<Recipe> recipes_;
    std::unordered_map<std::pair<std::string, int>, size_t, RecipeOutputKeyHash> byOutput_;
    std::unordered_set<std::string> runeNames_;
    std::unordered_set<std::string> rareRunes_;
    CachedItemNames outputNames_;
};
