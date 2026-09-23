#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "core/Config.h"
#include "core/ConfigManager.h"
#include "core/Logger.h"
#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OcrFrameDiffer.h"
#include "platform/PlatformPaths.h"
#include "price/PoeNinjaPriceProvider.h"
#include "price/PriceCache.h"
#include "recipes/RecipeDatabase.h"

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

void TestFormatting()
{
    Section("LootParser formatting");

    CheckEqual(LootParser::FormatPrice(1.0), "1 ex", "a whole price drops the decimals");
    CheckEqual(LootParser::FormatPrice(1.5), "1.5 ex", "a small price keeps one decimal");
    CheckEqual(LootParser::FormatPrice(250.0), "250 ex", "a large price has no decimals");
    CheckEqual(LootParser::FormatDivine(2.0), "2 div", "divine uses its own unit");
    CheckEqual(LootParser::FormatStack(2.0, 1, "ex"), "2 ex", "a single item shows no total");
    CheckEqual(LootParser::FormatStack(2.0, 3, "ex"), "2 ex (6 ex)", "a stack shows the total");
}

void TestNameMatching()
{
    Section("CachedItemNames::FindBest");

    const std::vector<std::string> names = {
        "Runic Alloy",
        "Mystic Alloy",
        "Perfect Exalted Orb",
        "The Runefather's Alloy",
    };

    const CachedItemNames cache = CachedItemNames::Build(names);

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
    TestLootParser();
    TestFormatting();
    TestNameMatching();
    TestFrameSimilarity();
    TestRecipeDatabase();
    TestPriceCacheDump();
    TestConfigLoadMalformed();
    TestRecipeDatabaseMalformedDownload();
    TestPriceCacheMalformedDump();
    TestPoeNinjaParsing();
    TestLoggerRepeats();

    std::filesystem::remove_all(sandbox, ec);

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);

    return gFailures == 0 ? 0 : 1;
}
