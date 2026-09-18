#include "recipes/RecipeDatabase.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "nlohmann/json.hpp"

#include "core/Logger.h"
#include "platform/PlatformPaths.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

using json = nlohmann::json;

namespace
{
std::string ToLower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string Trim(std::string_view s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return std::string(s.substr(begin, end - begin));
}
}

std::string RecipeDatabase::NormalizeRune(std::string_view name)
{
    std::string trimmed = Trim(name);

    constexpr std::string_view kSuffix = " Rune";
    if (trimmed.size() > kSuffix.size())
    {
        std::string_view tail(trimmed.data() + trimmed.size() - kSuffix.size(), kSuffix.size());
        if (ToLower(tail) == ToLower(kSuffix))
            trimmed = Trim(trimmed.substr(0, trimmed.size() - kSuffix.size()));
    }

    return trimmed;
}

bool RecipeDatabase::IsRareRune(const std::string& rune) const
{
    return rareRunes_.count(rune) > 0;
}

std::string RecipeDatabase::StripOcrNoise(std::string_view name)
{
    std::string text = Trim(name);

    while (!text.empty())
    {
        const unsigned char last = static_cast<unsigned char>(text.back());

        if (std::isalnum(last) || last == ')')
            break;

        text.pop_back();
        text = Trim(text);
    }

    const std::size_t space = text.find_last_of(' ');

    if (space != std::string::npos)
    {
        const std::string tail = text.substr(space + 1);

        const bool shortWord = tail.size() <= 2 && !tail.empty() &&
            std::all_of(tail.begin(), tail.end(),
                [](unsigned char c) { return std::isalpha(c) != 0; });

        if (shortWord)
            text = Trim(text.substr(0, space));
    }

    return text;
}

const Recipe* RecipeDatabase::FindRecipe(std::string_view output, int count) const
{
    auto it = byOutput_.find(std::pair{ ToLower(Trim(output)), count });

    if (it == byOutput_.end())
    {
        const std::string cleaned = StripOcrNoise(output);

        if (cleaned.empty())
            return nullptr;

        it = byOutput_.find(std::pair{ ToLower(cleaned), count });
    }

    if (it == byOutput_.end())
        return nullptr;

    return &recipes_[it->second];
}

namespace
{
std::string GeneratedAt(const std::filesystem::path& path)
{
    std::ifstream file(path);

    if (!file)
        return {};

    json parsed = json::parse(file, nullptr, false);

    if (parsed.is_discarded())
        return {};

    return parsed.value("generated", std::string());
}
}

std::filesystem::path DownloadedRecipeDatabasePath()
{
    return GetUserDataDir() / "combinations.downloaded.json";
}

bool RecipeDatabase::Load()
{
    const std::filesystem::path shipped = PrepareRecipeDatabase();
    const std::filesystem::path downloaded = DownloadedRecipeDatabasePath();

    std::error_code ec;

    if (std::filesystem::exists(downloaded, ec))
    {
        const std::string downloadedDate = GeneratedAt(downloaded);

        if (!downloadedDate.empty() && downloadedDate >= GeneratedAt(shipped))
        {
            if (LoadFromFile(downloaded))
                return true;

            LOG_ERROR("RecipeDatabase: downloaded database is unusable, falling back to the shipped one");
        }
    }

    if (shipped.empty())
    {
        LOG_ERROR("RecipeDatabase: combinations.json could not be prepared");
        return false;
    }

    return LoadFromFile(shipped);
}

bool RecipeDatabase::LoadFromFile(const std::filesystem::path& path)
{
    std::ifstream file(path);

    if (!file)
        return false;

    json j = json::parse(file, nullptr, false);

    if (j.is_discarded() || !j.contains("combinations") || !j["combinations"].is_array())
    {
        LOG_ERROR("RecipeDatabase: invalid combinations.json: " + path.string());
        return false;
    }

    std::vector<Recipe> recipes;
    std::set<std::string> runeNames;

    for (const auto& entry : j["combinations"])
    {
        Recipe recipe;
        recipe.output = entry.value("output", std::string());
        recipe.count = entry.value("count", 1);
        recipe.level = entry.value("level", 0);
        recipe.category = entry.value("category", std::string("unknown"));

        if (recipe.output.empty() || !entry.contains("runes") || !entry["runes"].is_array())
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
        LOG_ERROR("RecipeDatabase: no valid combinations in " + path.string());
        return false;
    }

    recipes_ = std::move(recipes);

    byOutput_.clear();

    for (size_t i = 0; i < recipes_.size(); ++i)
        byOutput_.emplace(std::pair{ ToLower(recipes_[i].output), recipes_[i].count }, i);

    runeNames_ = std::move(runeNames);

    rareRunes_.clear();

    if (j.contains("rareRunes") && j["rareRunes"].is_array())
    {
        for (const auto& entry : j["rareRunes"])
        {
            if (entry.is_string())
                rareRunes_.insert(NormalizeRune(entry.get<std::string>()));
        }
    }
    complete_ = j.value("complete", false);
    loadedFrom_ = path.string();
    loaded_ = true;

    LOG_INFO(
        "RecipeDatabase: loaded " + std::to_string(recipes_.size()) +
        " combinations (" + std::to_string(runeNames_.size()) + " rune types) from " + loadedFrom_ +
        (complete_ ? "" : " [PARTIAL dataset - run tools/scrape_poe2db.py]"));

    return true;
}
