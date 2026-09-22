#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <leptonica/allheaders.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/Config.h"
#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OCR.h"

namespace fs = std::filesystem;

namespace
{
struct Row
{
    int y = 0;
    int quantity = 1;
    std::string name;
    std::string matched;
    int confidence = 0;
};

struct TruthRow
{
    int quantity = 1;
    std::string name;
};

struct Score
{
    int truthRows = 0;
    int ignorableRows = 0;
    int detectedRows = 0;
    int phantom = 0;
    int missed = 0;
    int exactText = 0;
    int exactQuantity = 0;
    int priced = 0;
};

std::string Squash(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    for (unsigned char c : text)
    {
        if (std::isalnum(c))
            out.push_back(static_cast<char>(std::tolower(c)));
    }

    return out;
}

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

std::vector<Row> Recognize(OCR& ocr, const CachedItemNames& vocabulary, const fs::path& image)
{
    std::vector<Row> rows;

    const cv::Mat bgr = cv::imread(image.string(), cv::IMREAD_COLOR);

    if (bgr.empty())
        return rows;

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    AppConfig config;
    const std::vector<LootLine> loot = ocr.RecognizeLoot(gray, config);

    for (const LootLine& line : loot)
    {
        const auto parsed = LootParser::ParseLootLine(line.text);

        Row row;
        row.y = line.y1;
        row.quantity = parsed.quantity;
        row.name = parsed.itemName;
        row.matched = "-";

        if (const auto guess = vocabulary.FindBest(parsed.itemName))
        {
            row.matched = guess->name;
            row.confidence = guess->confidence;
        }

        rows.push_back(std::move(row));
    }

    return rows;
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

std::vector<TruthRow> LoadTruth(const fs::path& path)
{
    std::vector<TruthRow> rows;
    std::ifstream in(path);

    if (!in)
        return rows;

    std::string line;

    while (std::getline(in, line))
    {
        const std::size_t qtyAt = line.find("qty=");
        const std::size_t nameAt = line.find("name=\"");

        if (qtyAt == std::string::npos || nameAt == std::string::npos)
            continue;

        const std::size_t nameEnd = line.rfind('"');

        if (nameEnd <= nameAt + 6)
            continue;

        TruthRow row;
        row.quantity = std::atoi(line.c_str() + qtyAt + 4);
        row.name = line.substr(nameAt + 6, nameEnd - nameAt - 6);

        rows.push_back(std::move(row));
    }

    return rows;
}

bool Related(const std::string& truth, const Row& row)
{
    const std::string want = Squash(truth);
    const std::string got = Squash(row.name);

    if (want.empty() || got.empty())
        return false;

    if (Squash(row.matched) == want)
        return true;

    if (!got.starts_with(want) && !want.starts_with(got))
        return false;

    const std::size_t shorter = (std::min)(want.size(), got.size());
    const std::size_t longer = (std::max)(want.size(), got.size());

    return shorter * 2 >= longer;
}

Score ScorePanel(
    const std::vector<TruthRow>& truth,
    const std::vector<Row>& rows,
    const std::set<std::string>& vocabulary,
    std::vector<std::string>& issues
)
{
    const std::size_t n = truth.size();
    const std::size_t m = rows.size();

    std::vector<std::vector<int>> best(n + 1, std::vector<int>(m + 1, 0));

    for (std::size_t i = 1; i <= n; ++i)
    {
        for (std::size_t j = 1; j <= m; ++j)
        {
            best[i][j] = (std::max)(best[i - 1][j], best[i][j - 1]);

            if (Related(truth[i - 1].name, rows[j - 1]))
                best[i][j] = (std::max)(best[i][j], best[i - 1][j - 1] + 1);
        }
    }

    std::vector<std::pair<long, long>> pairs;
    std::size_t i = n;
    std::size_t j = m;

    while (i > 0 && j > 0)
    {
        if (Related(truth[i - 1].name, rows[j - 1]) && best[i][j] == best[i - 1][j - 1] + 1)
        {
            pairs.emplace_back(static_cast<long>(i - 1), static_cast<long>(j - 1));
            --i;
            --j;
        }
        else if (best[i][j] == best[i - 1][j])
        {
            pairs.emplace_back(static_cast<long>(i - 1), -1);
            --i;
        }
        else
        {
            pairs.emplace_back(-1, static_cast<long>(j - 1));
            --j;
        }
    }

    while (i > 0)
    {
        pairs.emplace_back(static_cast<long>(i - 1), -1);
        --i;
    }

    while (j > 0)
    {
        pairs.emplace_back(-1, static_cast<long>(j - 1));
        --j;
    }

    std::reverse(pairs.begin(), pairs.end());

    Score score;
    score.detectedRows = static_cast<int>(m);

    for (const TruthRow& row : truth)
    {
        if (vocabulary.count(row.name) > 0)
            ++score.truthRows;
    }

    char buffer[512];

    for (const auto& [ti, ji] : pairs)
    {
        if (ti < 0)
        {
            const Row& row = rows[static_cast<std::size_t>(ji)];
            std::snprintf(buffer, sizeof(buffer), "    PHANTOM  y=%d read as %dx \"%s\"", row.y, row.quantity, row.name.c_str());
            issues.emplace_back(buffer);
            ++score.phantom;
            continue;
        }

        const TruthRow& want = truth[static_cast<std::size_t>(ti)];
        const bool reportable = vocabulary.count(want.name) > 0;

        if (!reportable)
        {
            ++score.ignorableRows;
            continue;
        }

        if (ji < 0)
        {
            std::snprintf(buffer, sizeof(buffer), "    MISSED   %dx \"%s\"", want.quantity, want.name.c_str());
            issues.emplace_back(buffer);
            ++score.missed;
            continue;
        }

        const Row& row = rows[static_cast<std::size_t>(ji)];

        score.exactText += row.name == want.name;
        score.exactQuantity += row.quantity == want.quantity;
        score.priced += row.matched == want.name;

        if (row.name != want.name || row.quantity != want.quantity)
        {
            std::snprintf(
                buffer,
                sizeof(buffer),
                "    TEXT     truth %dx \"%s\" -> read %dx \"%s\"%s",
                want.quantity,
                want.name.c_str(),
                row.quantity,
                row.name.c_str(),
                row.matched == want.name ? "" : "   PRICE LOST"
            );

            issues.emplace_back(buffer);
        }
    }

    return score;
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
    if (argc < 6)
    {
        std::printf("usage: ocr_golden <tessdata> <combinations.json> <panels> <golden> <truth> [--bless]\n");
        return 2;
    }

    const fs::path tessdata = argv[1];
    const fs::path combinations = argv[2];
    const fs::path panels = argv[3];
    const fs::path golden = argv[4];
    const fs::path truth = argv[5];
    const bool bless = argc > 6 && std::string(argv[6]) == "--bless";

    cv::setNumThreads(1);
    setMsgSeverity(L_SEVERITY_NONE);

    OCR ocr;

    if (!ocr.Init(tessdata.string()))
    {
        std::printf("ocr_golden: OCR::Init failed for %s\n", tessdata.string().c_str());
        return 2;
    }

    const std::vector<std::string> vocabularyNames = LoadVocabulary(combinations);
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
        {
            const Score panel = ScorePanel(expectedRows, rows, knownNames, issues);

            total.truthRows += panel.truthRows;
            total.ignorableRows += panel.ignorableRows;
            total.detectedRows += panel.detectedRows;
            total.phantom += panel.phantom;
            total.missed += panel.missed;
            total.exactText += panel.exactText;
            total.exactQuantity += panel.exactQuantity;
            total.priced += panel.priced;
        }

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
