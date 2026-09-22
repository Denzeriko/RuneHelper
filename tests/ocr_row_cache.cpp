#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <leptonica/allheaders.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "core/Config.h"
#include "ocr/OCR.h"
#include "ocr/OcrRowCache.h"

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
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: ocr_row_cache <tessdata> <panels>\n");
        return 2;
    }

    const fs::path tessdata = argv[1];
    const fs::path panels = argv[2];

    cv::setNumThreads(1);
    cv::theRNG().state = 20260921;
    setMsgSeverity(L_SEVERITY_NONE);

    OCR ocr;

    if (!ocr.Init(tessdata.string()))
    {
        std::printf("ocr_row_cache: OCR::Init failed for %s\n", tessdata.string().c_str());
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

    const AppConfig config;

    std::vector<Failure> failures;

    std::size_t totalRows = 0;
    std::size_t repeatHits = 0;
    std::size_t noiseHits = 0;
    std::size_t noiseRows = 0;
    std::size_t editedHits = 0;
    std::size_t editedMisses = 0;
    std::size_t editedPanels = 0;
    std::size_t noiseStable = 0;
    std::size_t driftPanels = 0;
    std::size_t driftSteps = 0;

    for (const fs::path& image : images)
    {
        const std::string name = fs::relative(image, panels).generic_string();
        const cv::Mat gray = LoadGray(image);

        if (gray.empty())
        {
            failures.push_back({ name, "could not read the panel" });
            continue;
        }

        const std::vector<cv::Rect> rows = ocr.FindLootRows(gray);
        const std::string plain = Serialize(ocr.RecognizeLoot(gray, config));

        OcrRowCache cache;

        if (Serialize(ocr.RecognizeLoot(gray, config, &cache)) != plain)
        {
            failures.push_back({ name, "a cold cache changed the result" });
            continue;
        }

        if (cache.Hits() != 0)
        {
            failures.push_back({ name, "a cold cache reported hits" });
            continue;
        }

        const std::size_t rowCount = cache.Misses();
        totalRows += rowCount;

        cache.ResetCounters();

        if (Serialize(ocr.RecognizeLoot(gray, config, &cache)) != plain)
            failures.push_back({ name, "the same frame through a warm cache changed the result" });

        if (cache.Misses() != 0)
        {
            failures.push_back(
                { name, "the same frame re-read " + std::to_string(cache.Misses()) + " of " + std::to_string(rowCount) + " rows" }
            );
        }

        repeatHits += cache.Hits();

        cv::Mat edited;

        if (WithOneRowEdited(gray, rows, edited))
        {
            ++editedPanels;

            OcrRowCache editedCache;
            ocr.RecognizeLoot(gray, config, &editedCache);
            editedCache.ResetCounters();

            const std::string cached = Serialize(ocr.RecognizeLoot(edited, config, &editedCache));
            const std::string uncached = Serialize(ocr.RecognizeLoot(edited, config));

            if (cached != uncached)
            {
                failures.push_back({ name,
                                     "an edited frame through a warm cache did not match a plain run\n"
                                     "      original rows: " +
                                         DescribeRows(rows) +
                                         "\n"
                                         "      edited rows:   " +
                                         DescribeRows(ocr.FindLootRows(edited)) });
            }

            if (editedCache.Hits() == 0)
            {
                failures.push_back({ name,
                                     "a one-row edit reused nothing, so the cache saved no work\n"
                                     "      original rows: " +
                                         DescribeRows(rows) +
                                         "\n"
                                         "      edited rows:   " +
                                         DescribeRows(ocr.FindLootRows(edited)) });
            }

            editedHits += editedCache.Hits();
            editedMisses += editedCache.Misses();
        }

        {
            OcrRowCache driftCache;
            ocr.RecognizeLoot(gray, config, &driftCache);

            int reReadAt = 0;
            cv::Mat drifted;

            for (int step = 1; step <= 10 && reReadAt == 0; ++step)
            {
                if (!WithRowBrightened(gray, rows, 2 * step, drifted))
                    break;

                driftCache.ResetCounters();
                ocr.RecognizeLoot(drifted, config, &driftCache);

                if (driftCache.Misses() > 0)
                    reReadAt = step;
            }

            if (!drifted.empty())
            {
                ++driftPanels;

                if (reReadAt == 0)
                {
                    failures.push_back({ name,
                                         "a row brightened by 20 levels in 2-level steps was never re-read, "
                                         "so the cache drifts away from the image that produced its text" });
                }
                else
                {
                    driftSteps += static_cast<std::size_t>(reReadAt);
                }
            }
        }

        OcrRowCache noiseCache;
        ocr.RecognizeLoot(gray, config, &noiseCache);
        noiseCache.ResetCounters();

        const cv::Mat noisy = WithNoise(gray, 0.8);

        if (Serialize(ocr.RecognizeLoot(noisy, config, &noiseCache)) == plain)
            ++noiseStable;

        noiseHits += noiseCache.Hits();
        noiseRows += noiseCache.Hits() + noiseCache.Misses();
    }

    std::printf("panels                          %zu\n", images.size());
    std::printf("rows recognised on a cold cache %zu\n", totalRows);
    std::printf("rows reused, identical frame    %zu / %zu\n", repeatHits, totalRows);
    std::printf("rows reused, one row edited     %zu reused, %zu re-read, over %zu panels\n", editedHits, editedMisses, editedPanels);
    std::printf("rows reused, sigma 0.8 noise    %zu / %zu\n", noiseHits, noiseRows);
    std::printf("panels whose text held steady   %zu / %zu under noise\n", noiseStable, images.size());

    if (driftPanels > 0)
    {
        std::printf(
            "gradual drift caught after       %.1f steps of 2 levels, over %zu panels\n",
            static_cast<double>(driftSteps) / static_cast<double>(driftPanels),
            driftPanels
        );
    }

    if (totalRows > 0)
    {
        std::printf(
            "\nidentical frame avoids %.0f%% of the Tesseract calls\n",
            100.0 * static_cast<double>(repeatHits) / static_cast<double>(totalRows)
        );
    }

    if (editedHits + editedMisses > 0)
    {
        std::printf(
            "one edited row avoids %.0f%% of the Tesseract calls\n",
            100.0 * static_cast<double>(editedHits) / static_cast<double>(editedHits + editedMisses)
        );
    }

    if (failures.empty())
    {
        std::printf("\nok, %zu panels\n", images.size());
        return 0;
    }

    std::printf("\n%zu failures\n", failures.size());

    for (const Failure& failure : failures)
        std::printf("  %s: %s\n", failure.panel.c_str(), failure.reason.c_str());

    return 1;
}
