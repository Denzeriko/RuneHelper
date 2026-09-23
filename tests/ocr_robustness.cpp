#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <leptonica/allheaders.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "OcrScoring.h"
#include "core/Config.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OCR.h"
#include "platform/linux/ResourceHelper.h"

namespace fs = std::filesystem;

namespace
{
struct Case
{
    const char* label = "";
    double gain = 1.0;
    double offset = 0.0;
    double gamma = 1.0;
    double screenWidth = 0.0;
    bool looseCrop = false;
    int minimumPriced = 0;
};

struct Panel
{
    fs::path relative;
    double screenWidth = 0.0;
    cv::Mat gray;
    std::vector<TruthRow> truth;
};

cv::Mat ApplyLevels(const cv::Mat& gray, const Case& shift)
{
    cv::Mat lut(1, 256, CV_8U);

    for (int level = 0; level < 256; ++level)
    {
        const double curved = 255.0 * std::pow(level / 255.0, shift.gamma);
        lut.at<unsigned char>(level) = cv::saturate_cast<unsigned char>(shift.offset + shift.gain * curved);
    }

    cv::Mat shifted;
    cv::LUT(gray, lut, shifted);
    return shifted;
}

cv::Mat SurroundWithGame(const cv::Mat& gray)
{
    const int side = gray.cols / 5;
    const int top = gray.rows / 10;

    cv::Mat framed(gray.rows + 2 * top, gray.cols + 2 * side, CV_8UC1);
    cv::RNG rng(12345);
    rng.fill(framed, cv::RNG::UNIFORM, 15, 70);
    cv::GaussianBlur(framed, framed, cv::Size(9, 9), 3);
    gray.copyTo(framed(cv::Rect(side, top, gray.cols, gray.rows)));

    return framed;
}

cv::Mat Render(const Panel& panel, const Case& shift)
{
    cv::Mat gray = ApplyLevels(panel.gray, shift);

    if (shift.looseCrop)
        gray = SurroundWithGame(gray);

    if (shift.screenWidth > 0.0)
    {
        const double factor = shift.screenWidth / panel.screenWidth;
        cv::resize(gray, gray, cv::Size(), factor, factor, cv::INTER_CUBIC);
    }

    return gray;
}
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf("usage: ocr_robustness <combinations.json> <panels> <truth>\n");
        return 2;
    }

    const fs::path combinations = argv[1];
    const fs::path panelsDir = argv[2];
    const fs::path truthDir = argv[3];

    cv::setNumThreads(1);
    setMsgSeverity(L_SEVERITY_NONE);

    OCR ocr;

    if (!ocr.Init(EmbeddedTraineddata()))
    {
        std::printf("ocr_robustness: OCR::Init failed on the embedded traineddata\n");
        return 2;
    }

    const std::vector<std::string> vocabularyNames = LoadVocabulary(combinations);
    const std::set<std::string> knownNames(vocabularyNames.begin(), vocabularyNames.end());
    const CachedItemNames vocabulary = CachedItemNames::Build(vocabularyNames);

    std::vector<Panel> panels;

    for (const auto& entry : fs::recursive_directory_iterator(panelsDir))
    {
        if (entry.path().extension() != ".png")
            continue;

        Panel panel;
        panel.relative = fs::relative(entry.path(), panelsDir);
        panel.screenWidth = std::stod(panel.relative.begin()->string());
        panel.truth = LoadTruth(truthDir / (panel.relative.string() + ".txt"));

        const cv::Mat bgr = cv::imread(entry.path().string(), cv::IMREAD_COLOR);

        if (bgr.empty() || panel.truth.empty())
            continue;

        cv::cvtColor(bgr, panel.gray, cv::COLOR_BGR2GRAY);
        panels.push_back(std::move(panel));
    }

    std::sort(panels.begin(), panels.end(), [](const Panel& a, const Panel& b) { return a.relative < b.relative; });

    if (panels.empty())
    {
        std::printf("ocr_robustness: no labelled panels found in %s\n", panelsDir.string().c_str());
        return 2;
    }

    const std::vector<Case> cases = {
        { .label = "as captured", .minimumPriced = 192 },
        { .label = "dim, 0.7x", .gain = 0.7, .minimumPriced = 188 },
        { .label = "washed out, 45 + 0.65x", .gain = 0.65, .offset = 45.0, .minimumPriced = 188 },
        { .label = "overexposed, 1.3x", .gain = 1.3, .minimumPriced = 188 },
        { .label = "darker by 35", .offset = -35.0, .minimumPriced = 188 },
        { .label = "gamma 1.25", .gamma = 1.25, .minimumPriced = 188 },
        { .label = "gamma 0.8", .gamma = 0.8, .minimumPriced = 188 },
        { .label = "3200 wide screen", .screenWidth = 3200.0, .minimumPriced = 188 },
        { .label = "4K screen", .screenWidth = 3840.0, .minimumPriced = 188 },
        { .label = "4K screen, dim 0.7x", .gain = 0.7, .screenWidth = 3840.0, .minimumPriced = 188 },
        { .label = "loose crop over the game", .looseCrop = true, .minimumPriced = 170 },
        { .label = "loose crop, dim 0.7x", .gain = 0.7, .looseCrop = true, .minimumPriced = 180 },
    };

    const AppConfig config;
    int failed = 0;

    std::printf("%-28s %9s %9s %8s %7s %8s\n", "input", "priced", "exact", "phantom", "missed", "floor");

    for (const Case& shift : cases)
    {
        Score total;
        std::vector<std::string> issues;

        for (const Panel& panel : panels)
        {
            const std::vector<Row> rows = ToRows(ocr.RecognizeLoot(Render(panel, shift), config), vocabulary);
            total.Add(ScorePanel(panel.truth, rows, knownNames, issues));
        }

        const bool ok = total.priced >= shift.minimumPriced;
        failed += ok ? 0 : 1;

        std::printf(
            "%-28s %5d/%-3d %5d/%-3d %8d %7d %8d  %s\n",
            shift.label,
            total.priced,
            total.truthRows,
            total.exactText,
            total.truthRows,
            total.phantom,
            total.missed,
            shift.minimumPriced,
            ok ? "ok" : "BELOW FLOOR"
        );
    }

    std::printf("\n%d of %zu inputs priced fewer rows than their floor\n", failed, cases.size());

    return failed == 0 ? 0 : 1;
}
