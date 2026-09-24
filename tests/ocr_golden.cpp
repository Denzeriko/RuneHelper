#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "OcrScoring.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OCR.h"
#include "platform/linux/ResourceHelper.h"

namespace fs = std::filesystem;

namespace
{
std::vector<Row> Recognize(OCR& ocr, const CachedItemNames& vocabulary, const fs::path& image)
{
    const cv::Mat bgr = cv::imread(image.string(), cv::IMREAD_COLOR);

    if (bgr.empty())
        return {};

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    return ToRows(ocr.RecognizeLoot(gray), vocabulary);
}

std::string Serialize(const std::vector<Row>& rows)
{
    std::ostringstream out;
    out << "rows=" << rows.size() << '\n';

    for (const Row& row : rows)
    {
        out << "y=" << row.y << " qty=" << row.quantity << " name=\"" << row.name << '"' << " match=\"" << row.matched << '"'
            << " mconf=" << row.confidence << '\n';
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
            lcs[i][j] = want[i] == got[j] ? lcs[i + 1][j + 1] + 1 : (std::max)(lcs[i + 1][j], lcs[i][j + 1]);
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
        std::printf("usage: ocr_golden <combinations.json> <panels> <golden> <truth> [--bless] [--language <code>]\n");
        return 2;
    }

    const fs::path combinations = argv[1];
    const fs::path panels = argv[2];
    const fs::path golden = argv[3];
    const fs::path truth = argv[4];
    bool bless = false;
    std::string language = "en";

    for (int i = 5; i < argc; ++i)
    {
        const std::string flag = argv[i];

        if (flag == "--bless")
            bless = true;
        else if (flag == "--language" && i + 1 < argc)
            language = argv[++i];
    }

    cv::setNumThreads(1);

    OCR ocr;

    if (!ocr.Init(EmbeddedTextModel(language)))
    {
        std::printf("ocr_golden: OCR::Init failed on the embedded text model for '%s'\n", language.c_str());
        return 2;
    }

    const std::vector<std::string> vocabularyNames = LoadVocabulary(combinations, language);
    const std::set<std::string> knownNames(vocabularyNames.begin(), vocabularyNames.end());
    const CachedItemNames vocabulary = CachedItemNames::Build(vocabularyNames);

    if (vocabulary.Empty())
    {
        std::printf("ocr_golden: no vocabulary loaded from %s\n", combinations.string().c_str());
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
        std::printf("ocr_golden: no panels found in %s\n", panels.string().c_str());
        return 2;
    }

    fs::create_directories(golden);

    int failed = 0;
    Score total;
    std::vector<std::string> issues;

    for (const fs::path& image : images)
    {
        const fs::path relative = fs::relative(image, panels);
        const std::string name = relative.generic_string();
        const fs::path expectedPath = golden / (relative.string() + ".txt");
        const std::vector<Row> rows = Recognize(ocr, vocabulary, image);
        const std::string actual = Serialize(rows);

        const std::vector<TruthRow> expectedRows = LoadTruth(truth / (relative.string() + ".txt"));

        if (!expectedRows.empty())
            total.Add(ScorePanel(expectedRows, rows, knownNames, issues));

        if (bless)
        {
            fs::create_directories(expectedPath.parent_path());

            std::ofstream out(expectedPath, std::ios::binary | std::ios::trunc);
            out << actual;
            std::printf("blessed %s\n", name.c_str());
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

    if (total.truthRows > 0)
    {
        std::printf("\naccuracy against %s\n", truth.string().c_str());

        for (const std::string& issue : issues)
            std::printf("%s\n", issue.c_str());

        std::printf("\n  rows the tool should report  %d\n", total.truthRows);
        std::printf("  rows with no data behind them %d\n", total.ignorableRows);
        std::printf("  rows the detector produced   %d\n", total.detectedRows);
        std::printf("  phantom rows                 %d\n", total.phantom);
        std::printf("  rows never detected          %d\n", total.missed);
        std::printf("  text read exactly right      %d/%d\n", total.exactText, total.truthRows);
        std::printf("  quantity read right          %d/%d\n", total.exactQuantity, total.truthRows);
        std::printf("  priced as the right item     %d/%d\n", total.priced, total.truthRows);
    }

    if (bless)
        return 0;

    std::printf("\n%d of %zu images changed\n", failed, images.size());

    return failed == 0 ? 0 : 1;
}
