#include "NameNormalizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace
{
    constexpr size_t kMaxLen = 128;

    int BoundedLevenshteinDistance(std::string_view a, std::string_view b, int maxDistance)
    {
        if (a == b)
            return 0;

        if (a.empty())
            return static_cast<int>(b.size());

        if (b.empty())
            return static_cast<int>(a.size());

        if (b.size() > a.size())
            std::swap(a, b);

        const int n = static_cast<int>(a.size());
        const int m = static_cast<int>(b.size());

        if (m > static_cast<int>(kMaxLen))
            return maxDistance + 1;

        if (std::abs(n - m) > maxDistance)
            return maxDistance + 1;

        std::array<int, kMaxLen + 1> prev{};
        std::array<int, kMaxLen + 1> cur{};

        for (int j = 0; j <= m; ++j)
            prev[j] = j;

        for (int i = 1; i <= n; ++i)
        {
            cur[0] = i;

            int rowMin = cur[0];
            const char ca = a[i - 1];

            for (int j = 1; j <= m; ++j)
            {
                const int insertCost = cur[j - 1] + 1;
                const int deleteCost = prev[j] + 1;
                const int replaceCost = prev[j - 1] + (ca == b[j - 1] ? 0 : 1);

                int best = insertCost < deleteCost ? insertCost : deleteCost;
                best = best < replaceCost ? best : replaceCost;

                cur[j] = best;

                if (best < rowMin)
                    rowMin = best;
            }

            if (rowMin > maxDistance)
                return maxDistance + 1;

            std::swap(prev, cur);
        }

        return prev[m];
    }

    int SimilarityPercentNormalized(std::string_view a, std::string_view b, int minConfidence)
    {
        if (a.empty() || b.empty())
            return 0;

        if (a == b)
            return 100;

        const int maxLen = static_cast<int>((std::max)(a.size(), b.size()));
        const int maxAllowedDistance = (maxLen * (100 - minConfidence)) / 100;
        const int dist = BoundedLevenshteinDistance(a, b, maxAllowedDistance);

        if (dist > maxAllowedDistance)
            return 0;

        return 100 - static_cast<int>((static_cast<double>(dist) / maxLen) * 100.0);
    }
}

std::string NormalizeName(std::string_view s)
{
    std::string out;
    out.reserve(s.size());

    bool prevSpace = true;

    for (unsigned char ch : s)
    {
        if (std::isalnum(ch))
        {
            out.push_back(static_cast<char>(std::tolower(ch)));
            prevSpace = false;
        }
        else if (std::isspace(ch))
        {
            if (!prevSpace)
            {
                out.push_back(' ');
                prevSpace = true;
            }
        }
    }

    if (!out.empty() && out.back() == ' ')
        out.pop_back();

    return out;
}

std::vector<CachedItemName> BuildCachedItemNames(const std::vector<std::string>& names)
{
    std::vector<CachedItemName> result;
    result.reserve(names.size());

    for (const auto& name : names)
        result.push_back({ name, NormalizeName(name) });

    return result;
}

std::optional<MatchResult> FindBestItemMatch(std::string_view input, const std::vector<CachedItemName>& candidates, int minConfidence)
{
    const std::string normalizedInput = NormalizeName(input);

    if (normalizedInput.empty())
        return std::nullopt;

    int bestScore = 0;
    const CachedItemName* best = nullptr;

    for (const auto& item : candidates)
    {
        if (item.normalized.empty())
            continue;

        const int inputLen = static_cast<int>(normalizedInput.size());
        const int itemLen = static_cast<int>(item.normalized.size());
        const int maxLen = (std::max)(inputLen, itemLen);
        const int maxAllowedDistance = (maxLen * (100 - minConfidence)) / 100;

        if (std::abs(inputLen - itemLen) > maxAllowedDistance)
            continue;

        const int score = SimilarityPercentNormalized(normalizedInput, item.normalized, minConfidence);

        if (score > bestScore)
        {
            bestScore = score;
            best = &item;

            if (bestScore == 100)
                break;
        }
    }

    if (!best || bestScore < minConfidence)
        return std::nullopt;

    return MatchResult{ best->original, bestScore };
}