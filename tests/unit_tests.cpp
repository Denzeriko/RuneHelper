#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "core/Config.h"
#include "core/ConfigManager.h"
#include "core/Logger.h"
#include "ocr/LootParser.h"
#include "ocr/LootRows.h"
#include "ocr/NameMatcher.h"
#include "ocr/OcrFrameDiffer.h"
#include "platform/PlatformPaths.h"
#include "price/PoeNinjaPriceProvider.h"
#include "price/PriceCache.h"
#include "recipes/RecipeDatabase.h"
#include "ui/OverlayIcons.h"

namespace
{
int gChecks = 0;
int gFailures = 0;

void Check(bool ok, const std::string& what)
{
    ++gChecks;

    if (ok)
        return;

    ++gFailures;
    std::printf("  FAIL  %s\n", what.c_str());
}

void CheckEqual(const std::string& got, const std::string& want, const std::string& what)
{
    Check(got == want, what + "  (got \"" + got + "\", want \"" + want + "\")");
}

void CheckEqual(int got, int want, const std::string& what)
{
    Check(got == want, what + "  (got " + std::to_string(got) + ", want " + std::to_string(want) + ")");
}

void Section(const char* name)
{
    std::printf("%s\n", name);
}

AppConfig Normalized(const AppConfig& input)
{
    AppConfig config = input;
    ConfigManager::Normalize(config);
    return config;
}

void TestConfigLeagues()
{
    Section("ConfigManager::Normalize, price league");

    auto league = [](const std::string& in)
    {
        AppConfig config;
        config.priceLeague = in;
        return Normalized(config).priceLeague;
    };

    CheckEqual(league("Forbidden Rites"), "Forbidden Rites", "a known league survives");
    CheckEqual(league("Standard"), "Standard", "Standard survives");
    CheckEqual(league("Hardcore Runes of Aldur"), "HC Runes of Aldur", "the old name still migrates");
    CheckEqual(league("Rise of the Abyssal"), "Rise of the Abyssal", "an unknown league survives");
    CheckEqual(league("HC Rise of the Abyssal"), "HC Rise of the Abyssal", "an unknown hardcore league survives");
    CheckEqual(league("  Padded League  "), "Padded League", "surrounding spaces are trimmed");
    CheckEqual(league(""), "Forbidden Rites", "an empty league falls back");
    CheckEqual(league("   "), "Forbidden Rites", "a blank league falls back");
    CheckEqual(league("Bad\nLeague\tName"), "BadLeagueName", "control characters are dropped");
    CheckEqual(static_cast<int>(league(std::string(200, 'x')).size()), 64, "a long league is capped");
}

void TestConfigLanguage()
{
    Section("ConfigManager::Normalize, game language");

    auto language = [](const std::string& in)
    {
        AppConfig config;
        config.gameLanguage = in;
        return Normalized(config).gameLanguage;
    };

    CheckEqual(language("en"), "en", "English survives");
    CheckEqual(language("ko"), "ko", "a known language survives");
    CheckEqual(language("zh-TW"), "en", "a language the game client lacks falls back to English");
    CheckEqual(language("xx"), "en", "an unknown language falls back to English");
    CheckEqual(language(""), "en", "an empty language falls back to English");
}

void TestConfigClamps()
{
    Section("ConfigManager::Normalize, clamps");

    AppConfig wild;
    wild.regionW = -10;
    wild.regionH = -1;
    wild.overlayFontSize = 999;
    wild.priceRefreshMinutes = 1;
    wild.priceColorMedium = 50;
    wild.priceColorHigh = 10;
    wild.priceColorVeryHigh = 5;

    const AppConfig config = Normalized(wild);

    CheckEqual(config.regionW, 0, "a negative region width clamps to zero");
    CheckEqual(config.regionH, 0, "a negative region height clamps to zero");
    CheckEqual(config.overlayFontSize, 48, "the font size clamps to the maximum");
    CheckEqual(config.priceRefreshMinutes, 5, "the refresh interval clamps to the minimum");

    Check(config.priceColorMedium <= config.priceColorHigh, "the medium threshold does not exceed high");
    Check(config.priceColorHigh <= config.priceColorVeryHigh, "the high threshold does not exceed very high");

    AppConfig small;
    small.overlayFontSize = 1;
    CheckEqual(Normalized(small).overlayFontSize, 8, "the font size clamps to the minimum");
}

void TestLootParser()
{
    Section("LootParser::ParseLootLine");

    auto parse = [](const std::string& line) { return LootParser::ParseLootLine(line); };

    auto name = [&parse](const std::string& line) { return parse(line).itemName; };
    auto qty = [&parse](const std::string& line) { return parse(line).quantity; };

    CheckEqual(name("1x Runic Alloy"), "Runic Alloy", "a plain line gives the name");
    CheckEqual(qty("1x Runic Alloy"), 1, "a plain line gives the quantity");
    CheckEqual(qty("12x Runic Alloy"), 12, "a two digit quantity parses");
    CheckEqual(qty("ix Runic Alloy"), 1, "a lowercase i reads as one");
    CheckEqual(qty("Ix Runic Alloy"), 1, "an uppercase I reads as one");
    CheckEqual(qty("lx Runic Alloy"), 1, "a lowercase l reads as one");
    CheckEqual(qty("|x Runic Alloy"), 1, "a pipe reads as one");
    CheckEqual(qty("Sx Runic Alloy"), 5, "an S reads as five");
    CheckEqual(qty("Ox Runic Alloy"), 1, "a leading O parses as zero and falls back to one");
    CheckEqual(name("In Swift Alloy"), "Swift Alloy", "an x misread as n still ends the quantity");
    CheckEqual(name("Iw Mystic Alloy"), "Mystic Alloy", "an x misread as w still ends the quantity");
    CheckEqual(name("1x Ox Idol"), "Ox Idol", "a name that looks like a quantity survives behind the real one");
    CheckEqual(qty("3n Chaos Orb"), 3, "a digit before a misread x keeps its value");
    CheckEqual(name("Inspiration Rune"), "Inspiration Rune", "a name starting with In is left whole");
    CheckEqual(name("12n Chaos Orb"), "12n Chaos Orb", "only a single digit before n counts as a quantity");

    CheckEqual(name("Runic Alloy"), "Runic Alloy", "a line with no quantity keeps the name");
    CheckEqual(qty("Runic Alloy"), 1, "a line with no quantity defaults to one");

    CheckEqual(name("1x Runic Alloy a"), "Runic Alloy", "a trailing one character token is stripped");
    CheckEqual(name("1x Runic Alloy-"), "Runic Alloy", "a trailing dash is stripped");
    CheckEqual(name("1x Runic Alloy'"), "Runic Alloy", "a trailing apostrophe is stripped");
    CheckEqual(name("1x The Runefather's Alloy"), "The Runefather's Alloy", "an inner apostrophe is kept");
}

void TestOverlayAnchor()
{
    Section("OverlayTextX");

    AppConfig config;
    config.overlayOffsetX = 20;

    const cv::Rect region(100, 50, 400, 300);

    CheckEqual(OverlayTextX(region, std::nullopt, config), 520, "before the first read the text sits past the region");
    CheckEqual(OverlayTextX(region, cv::Rect(0, 0, 400, 300), config), 520, "a panel filling the region keeps the old place");
    CheckEqual(
        OverlayTextX(region, cv::Rect(40, 30, 300, 240), config),
        460,
        "a panel inside a loose region anchors the text to its edge"
    );
    CheckEqual(OverlayTextX(region, cv::Rect(0, 0, 900, 300), config), 520, "a panel never pushes the text past the region");
}

void TestFormatting()
{
    Section("LootParser formatting");

    CheckEqual(LootParser::FormatPrice(1.0), "1 ex", "a whole price drops the decimals");
    CheckEqual(LootParser::FormatPrice(1.5), "1.5 ex", "a small price keeps one decimal");
    CheckEqual(LootParser::FormatPrice(250.0), "250 ex", "a large price has no decimals");
    CheckEqual(LootParser::FormatAmount(2.0, "div"), "2 div", "the unit follows the amount");
    CheckEqual(LootParser::FormatStack(2.0, 1, "ex"), "2 ex", "a single item shows no total");
    CheckEqual(LootParser::FormatStack(2.0, 3, "ex"), "2 ex (6 ex)", "a stack shows the total");
}

void TestOverlayIcons()
{
    Section("Overlay icons");

    const std::string exalted = IconString(kExaltedOrbIcon);
    const std::string divine = IconString(kDivineOrbIcon);

    CheckEqual(WithIconLabels("2 " + exalted + " (6 " + exalted + ")"), "2 ex (6 ex)", "the stroke font shows the exalted label");
    CheckEqual(WithIconLabels("1.5 " + divine + " ?"), "1.5 div ?", "the stroke font shows the divine label");
    Check(FindOverlayIcon(U'e') == nullptr, "ordinary text is not an icon");

    for (const OverlayIcon& icon : kOverlayIcons)
    {
        const cv::Mat image = LoadIconImage(icon);
        Check(!image.empty() && image.type() == CV_8UC4, std::string(icon.file) + " is embedded with an alpha channel");
    }
}

void TestNameMatching()
{
    Section("NameMatcher::FindBest");

    const std::vector<std::string> names = {
        "Runic Alloy",
        "Mystic Alloy",
        "Perfect Exalted Orb",
        "The Runefather's Alloy",
    };

    const NameMatcher cache = NameMatcher::Build(names);

    Check(!cache.Empty(), "the cache is built");
    CheckEqual(static_cast<int>(cache.Size()), 4, "every name is kept");

    const auto exact = cache.FindBest("Runic Alloy");
    Check(exact.has_value(), "an exact name matches");

    if (exact)
    {
        CheckEqual(exact->name, "Runic Alloy", "an exact name returns itself");
        CheckEqual(exact->confidence, 100, "an exact name scores 100");
    }

    const auto oneEdit = cache.FindBest("Runlc Alloy");
    Check(oneEdit.has_value(), "one misread character still matches");

    if (oneEdit)
        CheckEqual(oneEdit->name, "Runic Alloy", "one misread character resolves to the right entry");

    Check(!cache.FindBest("Runlc Alioy").has_value(), "two misreads in an eleven character name fall outside the tolerance");

    const auto longer = cache.FindBest("Perfecl Exalted 0rb");
    Check(longer.has_value(), "a longer name absorbs more misreads");

    if (longer)
        CheckEqual(longer->name, "Perfect Exalted Orb", "a longer misread name resolves correctly");

    Check(!cache.FindBest("Completely Different Thing").has_value(), "an unrelated name is rejected");
    Check(!cache.FindBest("").has_value(), "an empty name is rejected");

    CheckEqual(NormalizeName("  The Runefather's ALLOY "), "the runefathers alloy", "normalisation folds case and punctuation");
}

void TestLocalizedNames()
{
    Section("Localized item names");

    CheckEqual(NormalizeName("Сфера ХАОСА"), "сфера хаоса", "Cyrillic folds to lower case");
    CheckEqual(NormalizeName("Очернённые"), "очерненные", "yo folds to ye");
    CheckEqual(NormalizeName("Chaossphäre"), "chaossphare", "German umlauts fold to the base letter");
    CheckEqual(NormalizeName("Orbe d'altération"), "orbe dalteration", "French accents fold and apostrophes drop");
    CheckEqual(NormalizeName("카오스 오브"), "카오스 오브", "Hangul is kept");
    CheckEqual(NormalizeName("混沌石（Ｌｖ２０）"), "混沌石lv20", "full width letters fold and brackets drop");

    const NameMatcher translations = NameMatcher::Build(std::vector<NameAlias>{
        { "Chaos Orb", "Сфера хаоса" },
        { "Chaos Orb", "카오스 오브" },
        { "Mirror of Kalandra", "Зеркало Каландры" },
    });

    const auto russian = translations.FindBest("Сфера хаоса");
    Check(russian && russian->name == "Chaos Orb", "a Russian name resolves to the English one");

    const auto misread = translations.FindBest("Сфера хаоcа");
    Check(misread && misread->name == "Chaos Orb", "a Latin c in place of a Cyrillic one is a single misread");

    const auto korean = translations.FindBest("카오스 오브");
    Check(korean && korean->name == "Chaos Orb", "a Korean name resolves to the English one");

    Check(!translations.FindBest("Сфера").has_value(), "a fragment of a name does not resolve");

    const auto parsed = LootParser::ParseLootLine("3х Сфера хаоса");
    CheckEqual(parsed.quantity, 3, "a Cyrillic x marks the quantity");
    CheckEqual(parsed.itemName, "Сфера хаоса", "the name follows a Cyrillic x");
    CheckEqual(
        LootParser::ParseLootLine("1x Сфера хаоса ъ").itemName,
        "Сфера хаоса",
        "a trailing one letter Cyrillic token is stripped"
    );
    CheckEqual(
        LootParser::ParseLootLine("1x 마술의 고대 룬").itemName,
        "마술의 고대 룬",
        "a one syllable Korean word at the end is kept"
    );
    CheckEqual(LootParser::ParseLootLine("고유 창").itemName, "고유 창", "a one syllable Korean noun is kept");
    CheckEqual(LootParser::ParseLootLine("ユニーク 盾").itemName, "ユニーク 盾", "a one kanji word is kept");
    CheckEqual(LootParser::ParseLootLine("1x Exalted Orb l").itemName, "Exalted Orb", "a trailing stray Latin letter is stripped");

    auto suffix = [](const std::string& line)
    {
        const auto result = LootParser::ParseLootLine(line);
        return std::to_string(result.quantity) + " " + result.itemName;
    };

    CheckEqual(suffix("Рунный сплав (2)"), "2 Рунный сплав", "a quantity in brackets after the name is read");
    CheckEqual(
        suffix("Чародейский расплав (Уровень 18) (1)"),
        "1 Чародейский расплав (Уровень 18)",
        "only the last bracket is the quantity"
    );
    CheckEqual(
        suffix("Чародейский расплав (Уровень 18)"),
        "1 Чародейский расплав (Уровень 18)",
        "a level in brackets is not a quantity"
    );
    CheckEqual(suffix("Thaumaturgic Flux (Level 20)"), "1 Thaumaturgic Flux (Level 20)", "an English level is not a quantity");
    CheckEqual(suffix("Рунный сплав (2"), "2 Рунный сплав", "a lost closing bracket still reads");
    CheckEqual(suffix("Рунный сплав 2)"), "2 Рунный сплав", "a lost opening bracket still reads");
    CheckEqual(suffix("Сфера хаоса(3)"), "3 Сфера хаоса", "a bracket glued to the name still reads");
    CheckEqual(suffix("Old Relic (I)"), "1 Old Relic (I)", "a roman numeral is not a quantity");
    CheckEqual(suffix("Orbe divino x1"), "1 Orbe divino", "a Spanish x quantity after the name is read");
    CheckEqual(suffix("Orbe de caos superior x12"), "12 Orbe de caos superior", "a two digit Spanish quantity is read");
    CheckEqual(suffix("Orbe de caos xl"), "1 Orbe de caos", "a misread one after x still reads");
    CheckEqual(suffix("3 Orbe Exaltado Maior"), "3 Orbe Exaltado Maior", "a Portuguese bare number in front is the quantity");
    CheckEqual(
        suffix("1 Fluxo Taumatúrgico (Nível 19)"),
        "1 Fluxo Taumatúrgico (Nível 19)",
        "a bare number keeps the level in the name"
    );
    CheckEqual(
        suffix("5 objetos monetarios aleatorios"),
        "5 objetos monetarios aleatorios",
        "a number opening the name reads as the quantity"
    );
    CheckEqual(suffix("Orbe divino"), "1 Orbe divino", "a name ending in o is not a quantity");
    CheckEqual(suffix("Ix Runic Alloy"), "1 Runic Alloy", "a letter one before x still uses the x rule");

    const std::vector<LootLine> loot = { { "2x Сфера хаоса", 0, 10, 100, 30, 99.0f } };
    const AppConfig config;
    const cv::Rect region(0, 0, 400, 300);

    const std::vector<FrameRow> translated = ParseLootRows(loot, region, config, &translations);
    CheckEqual(translated.front().name, "Chaos Orb", "a row is translated before pricing");
    CheckEqual(translated.front().quantity, 2, "translation keeps the quantity");
    CheckEqual(ParseLootRows(loot, region, config).front().name, "Сфера хаоса", "without translations the row keeps its text");

    RecipeDatabase database;

    if (!database.Load())
    {
        Check(false, "the embedded database loads for the translation checks");
        return;
    }

    const Recipe* legacy = database.FindRecipe("Наследие Альдура", 1);
    Check(legacy && legacy->output == "Aldur's Legacy", "a recipe is found by its Russian name");

    const NameMatcher koreanNames = database.Translations("ko");
    Check(!koreanNames.Empty(), "Korean names load from the embedded database");

    const auto orb = koreanNames.FindBest("카오스 오브");
    Check(orb && orb->name == "Chaos Orb", "the embedded Korean name of Chaos Orb resolves");

    Check(database.Translations("xx").Empty(), "an unknown language has no translations");
}

void TestFrameSimilarity()
{
    Section("SimilarImages");

    cv::Mat base(40, 200, CV_8UC1, cv::Scalar(200));
    cv::rectangle(base, cv::Rect(20, 10, 60, 20), cv::Scalar(30), cv::FILLED);

    Check(SimilarImages(base, base.clone()), "an identical image matches itself");

    cv::Mat nudged = base.clone();
    nudged += 3;
    Check(SimilarImages(base, nudged), "a shift below the pixel threshold still matches");

    cv::Mat changed = base.clone();
    cv::rectangle(changed, cv::Rect(120, 10, 60, 20), cv::Scalar(30), cv::FILLED);
    Check(!SimilarImages(base, changed), "a real change is detected");

    cv::Mat resized;
    cv::resize(base, resized, cv::Size(100, 40));
    Check(!SimilarImages(base, resized), "a different size never matches");

    Check(!SimilarImages(base, cv::Mat()), "an empty image never matches");
}

void TestRecipeDatabase()
{
    Section("RecipeDatabase");

    RecipeDatabase database;
    const bool loaded = database.Load();

    Check(loaded, "the embedded database loads");

    if (!loaded)
        return;

    Check(!database.Recipes().empty(), "the database has recipes");

    const Recipe& first = database.Recipes().front();
    const Recipe* found = database.FindRecipe(first.output, first.count);

    Check(found != nullptr, "a known output is found");

    if (found)
        CheckEqual(found->output, first.output, "the lookup returns the right recipe");

    Check(database.FindRecipe(first.output + " x", first.count) != nullptr, "a trailing OCR token is tolerated");
    Check(database.FindRecipe("Not A Real Output At All", 1) == nullptr, "an unknown output is not found");
}

void TestPriceCacheDump()
{
    Section("PriceCache dump round trip");

    PriceCache cache;
    cache.SetLeague("Unit Test League");

    CheckEqual(static_cast<int>(cache.GetPriceCount()), 0, "a fresh league starts empty");
    Check(!cache.GetPrice("Runic Alloy").has_value(), "an unknown item has no price");

    const std::uint64_t version = cache.Version();
    cache.SetLeague("Another League");
    Check(cache.Version() != version, "switching leagues bumps the version");
    CheckEqual(static_cast<int>(cache.GetPriceCount()), 0, "switching leagues clears the prices");
}

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void TestConfigLoadMalformed()
{
    Section("ConfigManager::Load, malformed files");

    const std::filesystem::path config = GetUserDataDir() / "config.json";
    std::filesystem::path aside = config;
    aside += ".bad";

    WriteText(config, R"({"regionX": "100", "regionY": 7, "ocrEnabled": "yes", "priceLeague": null, "overlayFontSize": 30})");

    {
        ConfigManager manager;
        bool loaded = false;

        try
        {
            loaded = manager.Load();
        }
        catch (const std::exception&)
        {
            Check(false, "a config with wrong-typed fields throws");
        }

        Check(loaded, "a config with wrong-typed fields still loads");

        const AppConfig values = manager.Snapshot();
        CheckEqual(values.regionX, 0, "a string region keeps the default");
        CheckEqual(values.regionY, 7, "a well-typed field next to it is read");
        Check(values.ocrEnabled, "a string boolean keeps the default");
        CheckEqual(values.priceLeague, "Forbidden Rites", "a null league keeps the default");
        CheckEqual(values.overlayFontSize, 30, "the font size is read");
        Check(values.overlayIcons, "a config written before the icon switch shows currency icons");
    }

    for (const char* text : { "null", "[1, 2]", "{broken" })
    {
        std::error_code ec;
        std::filesystem::remove(aside, ec);
        WriteText(config, text);

        ConfigManager manager;
        bool loaded = true;

        try
        {
            loaded = manager.Load();
        }
        catch (const std::exception&)
        {
            Check(false, std::string("an unreadable config throws: ") + text);
        }

        Check(!loaded, std::string("an unreadable config is rejected: ") + text);
        Check(std::filesystem::exists(aside), std::string("an unreadable config is kept as config.json.bad: ") + text);
        Check(!std::filesystem::exists(config), std::string("an unreadable config is moved, not copied: ") + text);
    }

    std::error_code ec;
    std::filesystem::remove(aside, ec);
}

void TestConfigSaveLoad()
{
    Section("ConfigManager, save and load");

    {
        ConfigManager manager;
        manager.Update([](AppConfig& config) { config.overlayIcons = false; });
        manager.Flush();
    }

    ConfigManager manager;
    Check(manager.Load(), "a saved config loads");
    Check(!manager.Snapshot().overlayIcons, "the currency icon switch survives a restart");
}

void TestRecipeDatabaseDownloadWithoutNames()
{
    Section("RecipeDatabase, downloaded update without localized names");

    const std::filesystem::path downloaded = DownloadedRecipeDatabasePath();
    WriteText(downloaded, R"({"generated": "2099-01-01", "combinations": [{"output": "Chaos Orb", "count": 1, "runes": ["Fire"]}]})");

    RecipeDatabase database;
    Check(database.Load(), "a newer download without names loads");
    CheckEqual(database.LoadedFrom(), PathToUtf8(downloaded), "the newer download is the one used");

    const Recipe* recipe = database.FindRecipe("Сфера хаоса", 1);
    Check(recipe && recipe->output == "Chaos Orb", "names missing from the download come from the built-in copy");

    std::error_code ec;
    std::filesystem::remove(downloaded, ec);
}

void TestRecipeDatabaseMalformedDownload()
{
    Section("RecipeDatabase, malformed downloaded update");

    RecipeDatabase shipped;
    Check(shipped.Load(), "the shipped database loads");

    const std::filesystem::path downloaded = DownloadedRecipeDatabasePath();

    const char* broken[] = {
        R"({"generated": "2099-01-01", "combinations": [1, {"output": 5}, {"output": "X", "count": "2", "runes": ["A"]}]})",
        R"({"generated": 20990101, "combinations": [{"output": "X", "count": 1, "runes": ["A"]}]})",
        R"({"generated": "2099-01-01", "combinations": {"output": "X"}})",
        R"(["not", "an", "object"])",
    };

    for (const char* text : broken)
    {
        WriteText(downloaded, text);

        RecipeDatabase database;
        bool loaded = false;

        try
        {
            loaded = database.Load();
        }
        catch (const std::exception&)
        {
            Check(false, std::string("a malformed download throws: ") + text);
        }

        Check(loaded, std::string("a malformed download falls back to the shipped database: ") + text);
        CheckEqual(
            static_cast<int>(database.Recipes().size()),
            static_cast<int>(shipped.Recipes().size()),
            std::string("the fallback has every shipped recipe: ") + text
        );
        Check(!std::filesystem::exists(downloaded), std::string("the malformed download is removed: ") + text);
    }

    Check(!RecipeDatabase::Accepts(nlohmann::json::parse(broken[0])), "an update with no valid entry is not accepted");
    Check(
        RecipeDatabase::Accepts(
            nlohmann::json::parse(R"({"generated": "2099-01-01", "combinations": [{"output": "X", "count": 2, "runes": ["A"]}]})")
        ),
        "a minimal valid update is accepted"
    );
}

void TestPriceCacheMalformedDump()
{
    Section("PriceCache, malformed dump");

    const std::filesystem::path dump = GetUserDataDir() / "prices_dump_Malformed_League.json";
    WriteText(
        dump,
        R"({"items": {"Runic Alloy": 1.5, "Broken": "x", "Null": null}, "divine_to_ex": "bad", "dump_updated_at": "bad"})"
    );

    PriceCache cache;

    try
    {
        cache.SetLeague("Malformed League");
    }
    catch (const std::exception&)
    {
        Check(false, "a malformed dump throws");
    }

    CheckEqual(static_cast<int>(cache.GetPriceCount()), 1, "only the numeric price is loaded");
    Check(cache.GetPrice("Runic Alloy") == 1.5, "the numeric price is kept");
    Check(cache.DivineRate() == 0.0, "a string divine rate is ignored");

    std::error_code ec;
    std::filesystem::remove(dump, ec);
}

void TestPoeNinjaParsing()
{
    Section("PoeNinjaPriceProvider::ParseCategoryDump");

    const auto parse = [](const char* text) { return PoeNinjaPriceProvider::ParseCategoryDump(nlohmann::json::parse(text)); };

    const PriceTable good = parse(
        R"({"core": {"rates": {"exalted": 400}}, "items": [{"id": "a", "name": "Runic Alloy"}, {"id": "b", "name": "Mystic Alloy"}],)"
        R"( "lines": [{"id": "a", "primaryValue": 0.5}, {"id": "b", "primaryValue": null}]})"
    );

    Check(good.complete, "a well formed category is complete");
    CheckEqual(static_cast<int>(good.items.size()), 1, "a null primaryValue is skipped");
    Check(good.items.count("Runic Alloy") == 1 && good.items.at("Runic Alloy").ex == 200.0, "the price is converted to exalted");

    struct Case
    {
        const char* text;
        bool complete;
    };

    const Case cases[] = {
        { R"([1, 2, 3])", false },
        { R"({"core": {"rates": []}, "items": [], "lines": []})", false },
        { R"({"core": {"rates": {"exalted": "400"}}, "items": [], "lines": []})", false },
        { R"({"core": {"rates": {"exalted": 400}}, "items": [1, "x", null], "lines": [7, {"id": 5}]})", true },
    };

    for (const Case& c : cases)
    {
        try
        {
            const PriceTable table = parse(c.text);
            Check(table.complete == c.complete && table.items.empty(), std::string("malformed poe.ninja JSON is handled: ") + c.text);
        }
        catch (const std::exception&)
        {
            Check(false, std::string("malformed poe.ninja JSON throws: ") + c.text);
        }
    }
}

void TestLoggerRepeats()
{
    Section("Logger, repeated messages");

    Logger::Instance().Init();

    for (int i = 0; i < 10; ++i)
        LOG_ERROR("unit test repeated message");

    LOG_INFO("unit test distinct message");

    const std::string log = ReadText(GetUserDataDir() / "runehelper.log");
    const std::string repeated = "unit test repeated message";

    int written = 0;

    for (std::size_t at = log.find(repeated); at != std::string::npos; at = log.find(repeated, at + 1))
        ++written;

    CheckEqual(written, 3, "a repeated message is written three times, then held back");
    Check(log.find("unit test distinct message") != std::string::npos, "a different message is still written");
}
}

int main()
{
    const std::filesystem::path sandbox = std::filesystem::temp_directory_path() / "runehelper-unit-tests";

    std::error_code ec;
    std::filesystem::remove_all(sandbox, ec);
    std::filesystem::create_directories(sandbox, ec);

    setenv("XDG_CONFIG_HOME", sandbox.c_str(), 1);

    TestConfigLeagues();
    TestConfigClamps();
    TestConfigLanguage();
    TestLootParser();
    TestOverlayAnchor();
    TestFormatting();
    TestOverlayIcons();
    TestNameMatching();
    TestLocalizedNames();
    TestFrameSimilarity();
    TestRecipeDatabase();
    TestPriceCacheDump();
    TestConfigLoadMalformed();
    TestConfigSaveLoad();
    TestRecipeDatabaseMalformedDownload();
    TestRecipeDatabaseDownloadWithoutNames();
    TestPriceCacheMalformedDump();
    TestPoeNinjaParsing();
    TestLoggerRepeats();

    std::filesystem::remove_all(sandbox, ec);

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);

    return gFailures == 0 ? 0 : 1;
}
