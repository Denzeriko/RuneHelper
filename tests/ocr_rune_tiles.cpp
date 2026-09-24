#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "TestScenes.h"
#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OCR.h"
#include "ocr/OcrRowCache.h"
#include "ocr/RuneTileLocator.h"
#include "platform/linux/ResourceHelper.h"

namespace fs = std::filesystem;

namespace
{
struct Case
{
    const char* label = "";
    double gain = 1.0;
    double offset = 0.0;
    double screenWidth = 0.0;
    Surroundings surroundings = Surroundings::None;
    int minimumPlaced = 0;
};

struct Panel
{
    std::string name;
    double screenWidth = 0.0;
    cv::Mat gray;
};

struct RecipeRow
{
    std::string key;
    std::vector<cv::Rect> tiles;
};

class TileReader
{
public:
    TileReader(OCR& ocr, const fs::path& combinations) : ocr_(ocr)
    {
        std::ifstream in(combinations);
        const nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);

        std::vector<std::string> outputs;

        if (!parsed.is_discarded() && parsed.contains("combinations"))
        {
            for (const auto& entry : parsed["combinations"])
            {
                const std::string output = entry.value("output", std::string());
                const int count = entry.value("count", 0);

                if (output.empty() || !entry.contains("runes"))
                    continue;

                runeCounts_[{ output, count }] = static_cast<int>(entry["runes"].size());
                outputs.push_back(output);
            }
        }

        names_ = CachedItemNames::Build(outputs);
    }

    bool Loaded() const { return !runeCounts_.empty(); }

    std::vector<RecipeRow> Read(const cv::Mat& gray)
    {
        OcrRowCache cache;
        const std::vector<LootLine> loot = ocr_.RecognizeLoot(gray, &cache);

        RuneTileLocator locator;
        locator.Analyze(gray, cache.Panel().value_or(cv::Rect(0, 0, gray.cols, gray.rows)), cache.Levels());

        std::vector<RecipeRow> rows;

        for (const LootLine& line : loot)
        {
            const auto parsed = LootParser::ParseLootLine(line.text);
            const auto guess = names_.FindBest(parsed.itemName);

            if (!guess)
                continue;

            const auto recipe = runeCounts_.find({ guess->name, parsed.quantity });

            if (recipe == runeCounts_.end())
                continue;

            RecipeRow row;
            row.key = guess->name + " x" + std::to_string(parsed.quantity);

            if (locator.Valid())
                row.tiles = locator.TilesForRow(gray, line.y1, recipe->second);

            rows.push_back(std::move(row));
        }

        return rows;
    }

private:
    OCR& ocr_;
    std::map<std::pair<std::string, int>, int> runeCounts_;
    CachedItemNames names_;
};

cv::Rect Expected(const cv::Rect& tile, const cv::Point& shift, double scale)
{
    return cv::Rect(
        static_cast<int>((tile.x + shift.x) * scale),
        static_cast<int>((tile.y + shift.y) * scale),
        static_cast<int>(tile.width * scale),
        static_cast<int>(tile.height * scale)
    );
}

bool SamePlace(const std::vector<cv::Rect>& expected, const std::vector<cv::Rect>& found, const cv::Point& shift, double scale)
{
    if (expected.empty() || expected.size() != found.size())
        return false;

    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const cv::Rect want = Expected(expected[i], shift, scale);
        const cv::Rect& got = found[i];
        const int tolerance = std::max(3, want.width / 8);

        if (std::abs(got.x - want.x) > tolerance || std::abs(got.y - want.y) > tolerance ||
            std::abs(got.width - want.width) > tolerance || std::abs(got.height - want.height) > tolerance)
        {
            return false;
        }
    }

    return true;
}
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: ocr_rune_tiles <combinations.json> <panels>\n");
        return 2;
    }

    cv::setNumThreads(1);

    OCR ocr;

    if (!ocr.Init(EmbeddedTextModel("en")))
    {
        std::printf("ocr_rune_tiles: OCR::Init failed on the embedded text model\n");
        return 2;
    }

    TileReader reader(ocr, argv[1]);

    if (!reader.Loaded())
    {
        std::printf("ocr_rune_tiles: no recipes loaded from %s\n", argv[1]);
        return 2;
    }

    const fs::path panelsDir = argv[2];
    std::vector<Panel> panels;

    for (const auto& entry : fs::recursive_directory_iterator(panelsDir))
    {
        if (entry.path().extension() != ".png")
            continue;

        Panel panel;
        const fs::path relative = fs::relative(entry.path(), panelsDir);
        panel.name = relative.generic_string();
        panel.screenWidth = std::stod(relative.begin()->string());

        const cv::Mat bgr = cv::imread(entry.path().string(), cv::IMREAD_COLOR);

        if (bgr.empty())
            continue;

        cv::cvtColor(bgr, panel.gray, cv::COLOR_BGR2GRAY);
        panels.push_back(std::move(panel));
    }

    std::sort(panels.begin(), panels.end(), [](const Panel& a, const Panel& b) { return a.name < b.name; });

    if (panels.empty())
    {
        std::printf("ocr_rune_tiles: no panels found in %s\n", panelsDir.string().c_str());
        return 2;
    }

    constexpr int kMinimumLocated = 195;

    std::map<std::string, std::vector<RecipeRow>> clean;
    int recipeRows = 0;
    int located = 0;

    for (const Panel& panel : panels)
    {
        clean[panel.name] = reader.Read(panel.gray);

        for (const RecipeRow& row : clean[panel.name])
        {
            ++recipeRows;
            located += row.tiles.empty() ? 0 : 1;
        }
    }

    int failed = located >= kMinimumLocated ? 0 : 1;

    std::printf(
        "rune tiles located on the clean panels  %d of %d recipe rows, floor %d  %s\n\n",
        located,
        recipeRows,
        kMinimumLocated,
        failed == 0 ? "ok" : "BELOW FLOOR"
    );

    const std::vector<Case> cases = {
        { .label = "dim, 0.7x", .gain = 0.7, .minimumPlaced = 190 },
        { .label = "darker by 35", .offset = -35.0, .minimumPlaced = 180 },
        { .label = "4K screen", .screenWidth = 3840.0, .minimumPlaced = 185 },
        { .label = "loose crop over the game", .surroundings = Surroundings::Dark, .minimumPlaced = 190 },
        { .label = "loose crop, busy game", .surroundings = Surroundings::Busy, .minimumPlaced = 190 },
    };

    std::printf("%-28s %16s %8s\n", "input", "same place", "floor");

    for (const Case& shift : cases)
    {
        int placed = 0;

        for (const Panel& panel : panels)
        {
            cv::Mat gray;
            panel.gray.convertTo(gray, -1, shift.gain, shift.offset);
            gray = SurroundWithGame(gray, shift.surroundings);

            double scale = 1.0;

            if (shift.screenWidth > 0.0 && shift.screenWidth != panel.screenWidth)
            {
                scale = shift.screenWidth / panel.screenWidth;
                cv::resize(gray, gray, cv::Size(), scale, scale, cv::INTER_CUBIC);
            }

            const cv::Point offset = shift.surroundings == Surroundings::None ? cv::Point(0, 0) : SurroundingOffset(panel.gray);
            const std::vector<RecipeRow> found = reader.Read(gray);

            for (const RecipeRow& expected : clean[panel.name])
            {
                const auto match =
                    std::find_if(found.begin(), found.end(), [&expected](const RecipeRow& row) { return row.key == expected.key; });

                if (match != found.end() && SamePlace(expected.tiles, match->tiles, offset, scale))
                    ++placed;
            }
        }

        const bool ok = placed >= shift.minimumPlaced;
        failed += ok ? 0 : 1;

        std::printf("%-28s %9d of %-4d %8d  %s\n", shift.label, placed, located, shift.minimumPlaced, ok ? "ok" : "BELOW FLOOR");
    }

    std::printf("\n%d checks fell below their floor\n", failed);

    return failed == 0 ? 0 : 1;
}
