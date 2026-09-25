#include "recipes/RecipeDatabase.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include "nlohmann/json.hpp"

#include "core/JsonRead.h"
#include "core/Logger.h"
#include "core/Text.h"
#include "platform/PlatformPaths.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

using json = nlohmann::json;

namespace
{
NameMatcher BuildNames(const std::set<std::pair<std::string, std::string>>& aliases)
{
    std::vector<NameAlias> names;
    names.reserve(aliases.size());

    for (const auto& [name, alias] : aliases)
        names.push_back({ name, alias });

    return NameMatcher::Build(names);
}

std::map<std::string, std::string> ReadNames(const json& entry)
{
    std::map<std::string, std::string> names;

    if (!entry.is_object() || !entry.contains("names") || !entry["names"].is_object())
        return names;

    for (const auto& [language, name] : entry["names"].items())
    {
        if (name.is_string() && !name.get<std::string>().empty())
            names.emplace(language, name.get<std::string>());
    }

    return names;
}

std::unordered_map<std::string, std::map<std::string, std::string>> NamesByOutput(const json* j)
{
    std::unordered_map<std::string, std::map<std::string, std::string>> names;

    if (!j || !j->is_object() || !j->contains("combinations") || !(*j)["combinations"].is_array())
        return names;

    for (const auto& entry : (*j)["combinations"])
    {
        std::map<std::string, std::string> localized = ReadNames(entry);
        const std::string output = JsonValue(entry, "output", std::string());

        if (!output.empty() && !localized.empty())
            names.emplace(output, std::move(localized));
    }

    return names;
}
}

std::string RecipeDatabase::NormalizeRune(std::string_view name)
{
    std::string_view trimmed = Trim(name);

    constexpr std::string_view kSuffix = " Rune";
    if (trimmed.size() > kSuffix.size() && ToLowerAscii(trimmed.substr(trimmed.size() - kSuffix.size())) == ToLowerAscii(kSuffix))
        trimmed = Trim(trimmed.substr(0, trimmed.size() - kSuffix.size()));

    return std::string(trimmed);
}

bool RecipeDatabase::IsRareRune(const std::string& rune) const
{
    return rareRunes_.count(rune) > 0;
}

std::string RecipeDatabase::StripOcrNoise(std::string_view name)
{
    std::string_view text = Trim(name);

    while (!text.empty())
    {
        const unsigned char last = static_cast<unsigned char>(text.back());

        if (std::isalnum(last) || last == ')')
            break;

        text = Trim(text.substr(0, text.size() - 1));
    }

    const std::size_t space = text.find_last_of(' ');

    if (space != std::string_view::npos)
    {
        const std::string_view tail = text.substr(space + 1);

        const bool shortWord = tail.size() <= 2 && !tail.empty() &&
                               std::all_of(tail.begin(), tail.end(), [](unsigned char c) { return std::isalpha(c) != 0; });

        if (shortWord)
            text = Trim(text.substr(0, space));
    }

    return std::string(text);
}

NameMatcher RecipeDatabase::Translations(std::string_view language) const
{
    std::set<std::pair<std::string, std::string>> aliases;

    for (const Recipe& recipe : recipes_)
    {
        const auto it = recipe.names.find(std::string(language));

        if (it != recipe.names.end())
            aliases.emplace(recipe.output, it->second);
    }

    return BuildNames(aliases);
}

const Recipe* RecipeDatabase::FindRecipe(std::string_view output, int count) const
{
    auto it = byOutput_.find(std::pair{ ToLowerAscii(Trim(output)), count });

    if (it == byOutput_.end())
    {
        const std::string cleaned = StripOcrNoise(output);

        if (!cleaned.empty())
            it = byOutput_.find(std::pair{ ToLowerAscii(cleaned), count });
    }

    if (it == byOutput_.end() && !outputNames_.Empty())
    {
        if (const auto guess = outputNames_.FindBest(output))
            it = byOutput_.find(std::pair{ ToLowerAscii(guess->name), count });
    }

    if (it == byOutput_.end())
        return nullptr;

    return &recipes_[it->second];
}

namespace
{
json ReadJson(const std::filesystem::path& path)
{
    std::ifstream file(path);

    if (!file)
        return json();

    json parsed = json::parse(file, nullptr, false);

    if (parsed.is_discarded())
        return json();

    return parsed;
}

json ParseJson(std::string_view text)
{
    if (text.empty())
        return json();

    json parsed = json::parse(text, nullptr, false);

    if (parsed.is_discarded())
        return json();

    return parsed;
}

std::string GeneratedAt(const json& parsed)
{
    return JsonValue(parsed, "generated", std::string());
}

void DiscardDownloaded(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);

    LOG_ERROR("RecipeDatabase: the downloaded database is unusable, removed it and fell back to the shipped one");
}
}

std::filesystem::path DownloadedRecipeDatabasePath()
{
    return GetUserDataDir() / "combinations.downloaded.json";
}

bool RecipeDatabase::Load()
{
    const json shippedJson = ParseJson(LoadEmbeddedRecipeDatabase());
    const std::filesystem::path downloaded = DownloadedRecipeDatabasePath();

    std::error_code ec;

    if (std::filesystem::exists(downloaded, ec))
    {
        const json downloadedJson = ReadJson(downloaded);
        const std::string downloadedDate = GeneratedAt(downloadedJson);

        if (downloadedDate.empty())
        {
            DiscardDownloaded(downloaded);
        }
        else if (downloadedDate >= GeneratedAt(shippedJson))
        {
            if (LoadFromJson(downloadedJson, PathToUtf8(downloaded), &shippedJson))
                return true;

            DiscardDownloaded(downloaded);
        }
    }

    if (shippedJson.is_null())
    {
        LOG_ERROR("RecipeDatabase: the embedded combinations.json could not be read");
        return false;
    }

    return LoadFromJson(shippedJson, "the database embedded in the binary");
}

bool RecipeDatabase::Accepts(const json& j)
{
    RecipeDatabase probe;
    return probe.LoadFromJson(j, "the downloaded update");
}

bool RecipeDatabase::LoadFromJson(const json& j, std::string_view source, const json* namesFallback)
{
    if (!j.is_object() || !j.contains("combinations") || !j["combinations"].is_array())
    {
        LOG_ERROR("RecipeDatabase: invalid combinations.json: " + std::string(source));
        return false;
    }

    std::vector<Recipe> recipes;
    std::unordered_set<std::string> runeNames;
    const auto fallbackNames = NamesByOutput(namesFallback);

    for (const auto& entry : j["combinations"])
    {
        Recipe recipe;
        recipe.output = JsonValue(entry, "output", std::string());
        recipe.names = ReadNames(entry);

        if (recipe.names.empty())
        {
            const auto fallback = fallbackNames.find(recipe.output);

            if (fallback != fallbackNames.end())
                recipe.names = fallback->second;
        }
        recipe.count = entry.contains("count") ? JsonValue(entry, "count", 0) : 1;

        if (recipe.output.empty() || recipe.count < 1 || !entry.contains("runes") || !entry["runes"].is_array())
            continue;

        for (const auto& runeJson : entry["runes"])
        {
            if (!runeJson.is_string())
                continue;

            const std::string rune = NormalizeRune(runeJson.get<std::string>());

            if (rune.empty())
                continue;

            recipe.runes.push_back(rune);
            runeNames.insert(rune);
        }

        if (!recipe.runes.empty())
            recipes.push_back(std::move(recipe));
    }

    if (recipes.empty())
    {
        LOG_ERROR("RecipeDatabase: no valid combinations in " + std::string(source));
        return false;
    }

    recipes_ = std::move(recipes);

    byOutput_.clear();

    std::set<std::pair<std::string, std::string>> aliases;

    for (size_t i = 0; i < recipes_.size(); ++i)
    {
        byOutput_.emplace(std::pair{ ToLowerAscii(recipes_[i].output), recipes_[i].count }, i);
        aliases.emplace(recipes_[i].output, recipes_[i].output);

        for (const auto& localized : recipes_[i].names)
            aliases.emplace(recipes_[i].output, localized.second);
    }

    outputNames_ = BuildNames(aliases);

    rareRunes_.clear();

    if (j.contains("rareRunes") && j["rareRunes"].is_array())
    {
        for (const auto& entry : j["rareRunes"])
        {
            if (entry.is_string())
                rareRunes_.insert(NormalizeRune(entry.get<std::string>()));
        }
    }
    complete_ = JsonValue(j, "complete", false);
    loadedFrom_ = std::string(source);
    loaded_ = true;

    LOG_INFO(
        "RecipeDatabase: loaded " + std::to_string(recipes_.size()) + " combinations (" + std::to_string(runeNames.size()) +
        " rune types) from " + loadedFrom_ + (complete_ ? "" : " [PARTIAL dataset - run tools/scrape_poe2db.py]")
    );

    return true;
}
