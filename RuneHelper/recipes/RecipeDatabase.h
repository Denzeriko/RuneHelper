#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ocr/NameNormalizer.h"
#include "recipes/RecipeTypes.h"

std::filesystem::path DownloadedRecipeDatabasePath();

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

    bool loaded_ = false;
    bool complete_ = false;
    std::string loadedFrom_;
    std::vector<Recipe> recipes_;
    std::map<std::pair<std::string, int>, size_t> byOutput_;
    std::set<std::string> runeNames_;
    std::set<std::string> rareRunes_;
    CachedItemNames outputNames_;
};
