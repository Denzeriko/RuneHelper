#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OCR.h"

struct Row
{
    int y = 0;
    int quantity = 1;
    std::string name;
    std::string matched = "-";
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

    void Add(const Score& other)
    {
        truthRows += other.truthRows;
        ignorableRows += other.ignorableRows;
        detectedRows += other.detectedRows;
        phantom += other.phantom;
        missed += other.missed;
        exactText += other.exactText;
        exactQuantity += other.exactQuantity;
        priced += other.priced;
    }
};

inline std::string Squash(const std::string& text)
{
    std::string out = NormalizeName(text);
    out.erase(std::remove(out.begin(), out.end(), ' '), out.end());
    return out;
}

inline std::vector<std::string> LoadVocabulary(const std::filesystem::path& combinations, const std::string& language = "en")
{
    std::ifstream in(combinations);
    nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);

    std::vector<std::string> names;

    if (parsed.is_discarded() || !parsed.contains("combinations"))
        return names;

    for (const auto& entry : parsed["combinations"])
    {
        const std::string output = language == "en" ? entry.value("output", std::string())
                                   : entry.contains("names") && entry["names"].is_object()
                                       ? entry["names"].value(language, std::string())
                                       : std::string();

        if (!output.empty())
            names.push_back(output);
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    return names;
}

inline std::vector<Row> ToRows(const std::vector<LootLine>& loot, const CachedItemNames& vocabulary)
{
    std::vector<Row> rows;

    for (const LootLine& line : loot)
    {
        const auto parsed = LootParser::ParseLootLine(line.text);

        Row row;
        row.y = line.y1;
        row.quantity = parsed.quantity;
        row.name = parsed.itemName;

        if (const auto guess = vocabulary.FindBest(parsed.itemName))
        {
            row.matched = guess->name;
            row.confidence = guess->confidence;
        }

        rows.push_back(std::move(row));
    }

    return rows;
}

inline std::vector<TruthRow> LoadTruth(const std::filesystem::path& path)
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

inline bool Related(const std::string& truth, const Row& row)
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

inline Score ScorePanel(
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
