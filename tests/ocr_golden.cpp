#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <leptonica/allheaders.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "core/Config.h"
#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OCR.h"

namespace fs = std::filesystem;

namespace
{
std::vector<std::string> LoadVocabulary(const fs::path& combinations)
{
    std::ifstream in(combinations);
    nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);

    std::vector<std::string> names;

    if (parsed.is_discarded() || !parsed.contains("combinations"))
        return names;

    for (const auto& entry : parsed["combinations"])
    {
        const std::string output = entry.value("output", std::string());

        if (!output.empty())
            names.push_back(output);
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    return names;
}

std::string Render(OCR& ocr, const CachedItemNames& vocabulary, const fs::path& image)
{
    const cv::Mat bgr = cv::imread(image.string(), cv::IMREAD_COLOR);

    if (bgr.empty())
        return "error=could not read image\n";

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    AppConfig config;
    const std::vector<LootLine> loot = ocr.RecognizeLoot(gray, config);

    std::ostringstream out;
    out << "rows=" << loot.size() << '\n';

    for (const LootLine& line : loot)
    {
        const auto parsed = LootParser::ParseLootLine(line.text);

        std::string matched = "-";
        int confidence = 0;

        if (const auto guess = vocabulary.FindBest(parsed.itemName))
        {
            matched = guess->name;
            confidence = guess->confidence;
        }

        out << "y=" << line.y1
            << " qty=" << parsed.quantity
            << " name=\"" << parsed.itemName << '"'
            << " match=\"" << matched << '"'
            << " mconf=" << confidence
            << '\n';
    }

    return out.str();
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);

    if (!in)
        return {};

    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line))
        lines.push_back(line);

    return lines;
}

void PrintDiff(const std::string& name, const std::string& expected, const std::string& actual)
{
    const std::vector<std::string> want = SplitLines(expected);
    const std::vector<std::string> got = SplitLines(actual);

    const std::size_t n = want.size();
    const std::size_t m = got.size();

    std::vector<std::vector<std::size_t>> lcs(n + 1, std::vector<std::size_t>(m + 1, 0));

    for (std::size_t i = n; i-- > 0;)
    {
        for (std::size_t j = m; j-- > 0;)
        {
            lcs[i][j] = want[i] == got[j]
                ? lcs[i + 1][j + 1] + 1
                : (std::max)(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }

    std::printf("--- %s\n", name.c_str());

    std::size_t i = 0;
    std::size_t j = 0;

    while (i < n && j < m)
    {
        if (want[i] == got[j])
        {
            ++i;
            ++j;
        }
        else if (lcs[i + 1][j] >= lcs[i][j + 1])
        {
            std::printf("  - %s\n", want[i].c_str());
            ++i;
        }
        else
        {
            std::printf("  + %s\n", got[j].c_str());
            ++j;
        }
    }

    for (; i < n; ++i)
        std::printf("  - %s\n", want[i].c_str());

    for (; j < m; ++j)
        std::printf("  + %s\n", got[j].c_str());
}
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("usage: ocr_golden <tessdata> <combinations.json> <assets> <golden> [--bless]\n");
        return 2;
    }

    const fs::path tessdata = argv[1];
    const fs::path combinations = argv[2];
    const fs::path assets = argv[3];
    const fs::path golden = argv[4];
    const bool bless = argc > 5 && std::string(argv[5]) == "--bless";

    cv::setNumThreads(1);
    setMsgSeverity(L_SEVERITY_NONE);

    OCR ocr;

    if (!ocr.Init(tessdata.string()))
    {
        std::printf("ocr_golden: OCR::Init failed for %s\n", tessdata.string().c_str());
        return 2;
    }

    const CachedItemNames vocabulary = CachedItemNames::Build(LoadVocabulary(combinations));

    if (vocabulary.Empty())
    {
        std::printf("ocr_golden: no vocabulary loaded from %s\n", combinations.string().c_str());
        return 2;
    }

    std::vector<fs::path> images;

    for (const auto& entry : fs::directory_iterator(assets))
    {
        const std::string name = entry.path().filename().string();

        if (entry.path().extension() == ".png" && name.rfind("test", 0) == 0)
            images.push_back(entry.path());
    }

    std::sort(images.begin(), images.end());

    if (images.empty())
    {
        std::printf("ocr_golden: no test*.png found in %s\n", assets.string().c_str());
        return 2;
    }

    fs::create_directories(golden);

    int failed = 0;

    for (const fs::path& image : images)
    {
        const std::string name = image.filename().string();
        const fs::path expectedPath = golden / (name + ".txt");
        const std::string actual = Render(ocr, vocabulary, image);

        if (bless)
        {
            std::ofstream out(expectedPath, std::ios::binary | std::ios::trunc);
            out << actual;
            std::printf("blessed %s\n", expectedPath.filename().string().c_str());
            continue;
        }

        const std::string expected = ReadFile(expectedPath);

        if (expected.empty())
        {
            std::printf("MISSING %s (run with --bless to create it)\n", expectedPath.string().c_str());
            ++failed;
            continue;
        }

        if (expected == actual)
        {
            std::printf("ok      %s\n", name.c_str());
            continue;
        }

        std::printf("CHANGED %s\n", name.c_str());
        PrintDiff(name, expected, actual);
        ++failed;
    }

    if (bless)
        return 0;

    std::printf("\n%d of %zu images changed\n", failed, images.size());

    return failed == 0 ? 0 : 1;
}
