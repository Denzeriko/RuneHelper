#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "TestScenes.h"
#include "ocr/OCR.h"
#include "ocr/OcrRowCache.h"
#include "ocr/RowFinder.h"
#include "platform/linux/ResourceHelper.h"

namespace fs = std::filesystem;

namespace
{
struct Failure
{
    std::string panel;
    std::string reason;
};

std::string Serialize(const std::vector<LootLine>& lines)
{
    std::ostringstream out;

    for (const LootLine& line : lines)
        out << line.y1 << ':' << line.x1 << ':' << line.text << '\n';

    return out.str();
}

cv::Mat LoadGray(const fs::path& image)
{
    const cv::Mat bgr = cv::imread(image.string(), cv::IMREAD_COLOR);

    if (bgr.empty())
        return {};

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    return gray;
}

cv::Mat WithNoise(const cv::Mat& gray, double sigma)
{
    cv::Mat noise(gray.size(), CV_8SC1);
    cv::randn(noise, 0.0, sigma);

    cv::Mat out;
    cv::add(gray, noise, out, cv::noArray(), CV_8UC1);

    return out;
}

bool WithOneRowEdited(const cv::Mat& gray, const std::vector<cv::Rect>& rows, cv::Mat& edited)
{
    if (rows.size() < 3)
        return false;

    const cv::Rect& row = rows[rows.size() / 2];

    const int width = std::max(10, row.width / 6);
    const int height = std::max(3, row.height / 3);
    const int x = row.x + row.width * 3 / 4;
    const int y = row.y + (row.height - height) / 2;

    const cv::Rect patch = cv::Rect(x, y, width, height) & cv::Rect(0, 0, gray.cols, gray.rows);

    if (patch.width < 10 || patch.height < 3)
        return false;

    edited = gray.clone();
    edited(patch).setTo(cv::Scalar(0));

    return true;
}

bool WithRowBrightened(const cv::Mat& gray, const std::vector<cv::Rect>& rows, int delta, cv::Mat& out)
{
    if (rows.size() < 3)
        return false;

    const cv::Rect& row = rows[rows.size() / 2];

    const int x = row.x + row.width * 3 / 5;
    const cv::Rect patch = cv::Rect(x, row.y, row.width - (x - row.x), row.height) & cv::Rect(0, 0, gray.cols, gray.rows);

    if (patch.width < 10 || patch.height < 3)
        return false;

    out = gray.clone();
    out(patch) += static_cast<unsigned char>(delta);

    return true;
}

std::string DescribeRows(const std::vector<cv::Rect>& rows)
{
    std::ostringstream out;
    out << rows.size() << " rows";

    for (const cv::Rect& row : rows)
        out << " [y=" << row.y << " h=" << row.height << ']';

    return out.str();
}

struct Totals
{
    std::size_t rows = 0;
    std::size_t repeatHits = 0;
    std::size_t noiseHits = 0;
    std::size_t noiseRows = 0;
    std::size_t editedHits = 0;
    std::size_t editedMisses = 0;
    std::size_t editedPanels = 0;
    std::size_t noiseStable = 0;
    std::size_t driftPanels = 0;
    std::size_t driftSteps = 0;
};

std::string ReadThroughColdCache(
    OCR& ocr,
    const cv::Mat& gray,
    const std::optional<TextLevels>& levels,
    const std::optional<cv::Rect>& panel
)
{
    OcrRowCache cold;
    cold.SetLevels(levels);

    if (panel)
        cold.SetPanel(*panel);

    return Serialize(ocr.RecognizeLoot(gray, &cold));
}

void Exercise(
    OCR& ocr,
    const std::string& name,
    const cv::Mat& gray,
    const std::vector<cv::Rect>& rows,
    Totals& totals,
    std::vector<Failure>& failures
)
{
    const std::string plain = Serialize(ocr.RecognizeLoot(gray));

    OcrRowCache cache;

    if (Serialize(ocr.RecognizeLoot(gray, &cache)) != plain)
    {
        failures.push_back({ name, "a cold cache changed the result" });
        return;
    }

    if (cache.Hits() != 0)
    {
        failures.push_back({ name, "a cold cache reported hits" });
        return;
    }

    const std::size_t rowCount = cache.Misses();
    totals.rows += rowCount;

    cache.ResetCounters();

    if (Serialize(ocr.RecognizeLoot(gray, &cache)) != plain)
        failures.push_back({ name, "the same frame through a warm cache changed the result" });

    if (cache.Misses() != 0)
    {
        failures.push_back(
            { name, "the same frame re-read " + std::to_string(cache.Misses()) + " of " + std::to_string(rowCount) + " rows" }
        );
    }

    totals.repeatHits += cache.Hits();

    cv::Mat edited;

    if (WithOneRowEdited(gray, rows, edited))
    {
        ++totals.editedPanels;

        OcrRowCache editedCache;
        ocr.RecognizeLoot(gray, &editedCache);
        editedCache.ResetCounters();

        const std::optional<TextLevels> heldLevels = editedCache.Levels();
        const std::optional<cv::Rect> heldPanel = editedCache.Panel();
        const std::string cached = Serialize(ocr.RecognizeLoot(edited, &editedCache));
        const std::string uncached = ReadThroughColdCache(ocr, edited, heldLevels, heldPanel);

        if (cached != uncached)
        {
            failures.push_back({ name,
                                 "an edited frame through a warm cache did not match a plain run\n"
                                 "      original rows: " +
                                     DescribeRows(rows) +
                                     "\n"
                                     "      edited rows:   " +
                                     DescribeRows(FindLootRows(edited)) });
        }

        if (editedCache.Hits() == 0)
        {
            failures.push_back({ name,
                                 "a one-row edit reused nothing, so the cache saved no work\n"
                                 "      original rows: " +
                                     DescribeRows(rows) +
                                     "\n"
                                     "      edited rows:   " +
                                     DescribeRows(FindLootRows(edited)) });
        }

        totals.editedHits += editedCache.Hits();
        totals.editedMisses += editedCache.Misses();
    }

    {
        OcrRowCache driftCache;
        ocr.RecognizeLoot(gray, &driftCache);

        int reReadAt = 0;
        cv::Mat drifted;

        for (int step = 1; step <= 10 && reReadAt == 0; ++step)
        {
            if (!WithRowBrightened(gray, rows, 2 * step, drifted))
                break;

            driftCache.ResetCounters();
            ocr.RecognizeLoot(drifted, &driftCache);

            if (driftCache.Misses() > 0)
                reReadAt = step;
        }

        if (!drifted.empty())
        {
            ++totals.driftPanels;

            if (reReadAt == 0)
            {
                failures.push_back({ name,
                                     "a row brightened by 20 levels in 2-level steps was never re-read, "
                                     "so the cache drifts away from the image that produced its text" });
            }
            else
            {
                totals.driftSteps += static_cast<std::size_t>(reReadAt);
            }
        }
    }

    OcrRowCache noiseCache;
    ocr.RecognizeLoot(gray, &noiseCache);
    noiseCache.ResetCounters();

    const cv::Mat noisy = WithNoise(gray, 0.8);

    if (Serialize(ocr.RecognizeLoot(noisy, &noiseCache)) == plain)
        ++totals.noiseStable;

    totals.noiseHits += noiseCache.Hits();
    totals.noiseRows += noiseCache.Hits() + noiseCache.Misses();
}

void Report(const char* title, const Totals& totals, std::size_t panels)
{
    std::printf("%s\n", title);
    std::printf("panels                          %zu\n", panels);
    std::printf("rows recognised on a cold cache %zu\n", totals.rows);
    std::printf("rows reused, identical frame    %zu / %zu\n", totals.repeatHits, totals.rows);
    std::printf(
        "rows reused, one row edited     %zu reused, %zu re-read, over %zu panels\n",
        totals.editedHits,
        totals.editedMisses,
        totals.editedPanels
    );
    std::printf("rows reused, sigma 0.8 noise    %zu / %zu\n", totals.noiseHits, totals.noiseRows);
    std::printf("panels whose text held steady   %zu / %zu under noise\n", totals.noiseStable, panels);

    if (totals.driftPanels > 0)
    {
        std::printf(
            "gradual drift caught after       %.1f steps of 2 levels, over %zu panels\n",
            static_cast<double>(totals.driftSteps) / static_cast<double>(totals.driftPanels),
            totals.driftPanels
        );
    }

    if (totals.rows > 0)
    {
        std::printf(
            "identical frame avoids %.0f%% of the row reads\n",
            100.0 * static_cast<double>(totals.repeatHits) / static_cast<double>(totals.rows)
        );
    }

    if (totals.editedHits + totals.editedMisses > 0)
    {
        std::printf(
            "one edited row avoids %.0f%% of the row reads\n",
            100.0 * static_cast<double>(totals.editedHits) / static_cast<double>(totals.editedHits + totals.editedMisses)
        );
    }

    std::printf("\n");
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: ocr_row_cache <panels>\n");
        return 2;
    }

    const fs::path panels = argv[1];

    cv::setNumThreads(1);
    cv::theRNG().state = 20260921;

    OCR ocr;

    if (!ocr.Init(EmbeddedTextModel("en")))
    {
        std::printf("ocr_row_cache: OCR::Init failed on the embedded text model\n");
        return 2;
    }

    std::vector<fs::path> images;

    for (const auto& entry : fs::recursive_directory_iterator(panels))
    {
        if (entry.path().extension() == ".png")
            images.push_back(entry.path());
    }

    std::sort(images.begin(), images.end());

    if (images.empty())
    {
        std::printf("ocr_row_cache: no panels found in %s\n", panels.string().c_str());
        return 2;
    }

    std::vector<Failure> failures;
    std::vector<cv::Mat> grays;
    std::vector<std::vector<cv::Rect>> rows;

    for (const fs::path& image : images)
    {
        grays.push_back(LoadGray(image));
        rows.push_back(grays.back().empty() ? std::vector<cv::Rect>{} : FindLootRows(grays.back()));
    }

    Totals captured;
    Totals dimmed;

    for (std::size_t i = 0; i < images.size(); ++i)
    {
        const std::string name = fs::relative(images[i], panels).generic_string();

        if (grays[i].empty())
        {
            failures.push_back({ name, "could not read the panel" });
            continue;
        }

        Exercise(ocr, name, grays[i], rows[i], captured, failures);
    }

    for (std::size_t i = 0; i < images.size(); ++i)
    {
        if (grays[i].empty())
            continue;

        cv::Mat dim;
        grays[i].convertTo(dim, -1, 0.7, 0.0);

        Exercise(ocr, fs::relative(images[i], panels).generic_string() + " dimmed", dim, rows[i], dimmed, failures);
    }

    Totals surrounded;

    for (std::size_t i = 0; i < images.size(); ++i)
    {
        if (grays[i].empty())
            continue;

        const cv::Point offset = SurroundingOffset(grays[i]);
        std::vector<cv::Rect> shifted;

        for (const cv::Rect& row : rows[i])
            shifted.push_back(row + offset);

        Exercise(
            ocr,
            fs::relative(images[i], panels).generic_string() + " inside a busy scene",
            SurroundWithGame(grays[i], Surroundings::Busy),
            shifted,
            surrounded,
            failures
        );
    }

    Report("as captured", captured, images.size());
    Report("dimmed to 70%, brightness normalised on every frame", dimmed, images.size());
    Report("inside a busy game scene, only the loot panel is read", surrounded, images.size());

    if (failures.empty())
    {
        std::printf("ok, %zu panels\n", images.size());
        return 0;
    }

    std::printf("%zu failures\n", failures.size());

    for (const Failure& failure : failures)
        std::printf("  %s: %s\n", failure.panel.c_str(), failure.reason.c_str());

    return 1;
}
