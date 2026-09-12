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

    CachedItemNames::Histogram MakeHistogram(std::string_view text)
    {
        CachedItemNames::Histogram histogram{};

        for (unsigned char ch : text)
        {
            std::size_t bucket = CachedItemNames::kHistogramSize - 1;

            if (ch >= 'a' && ch <= 'z')
                bucket = static_cast<std::size_t>(ch - 'a');
            else if (ch >= '0' && ch <= '9')
                bucket = 26 + static_cast<std::size_t>(ch - '0');

            if (histogram[bucket] < 255)
                ++histogram[bucket];
        }

        return histogram;
    }

    bool HistogramAllows(
        const CachedItemNames::Histogram& a,
        const CachedItemNames::Histogram& b,
        int maxDistance)
    {
        const int limit = 2 * maxDistance;
        int difference = 0;

        for (std::size_t i = 0; i < CachedItemNames::kHistogramSize; ++i)
        {
            difference += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));

            if (difference > limit)
                return false;
        }

        return true;
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

CachedItemNames CachedItemNames::Build(const std::vector<std::string>& names)
{
    CachedItemNames cache;
    cache.entries_.reserve(names.size());

    for (const auto& name : names)
    {
        std::string normalized = NormalizeName(name);

        if (normalized.empty())
            continue;

        Histogram histogram = MakeHistogram(normalized);

        cache.entries_.push_back({ name, std::move(normalized), histogram });
    }

    std::sort(
        cache.entries_.begin(),
        cache.entries_.end(),
        [](const Entry& a, const Entry& b)
        {
            if (a.normalized.size() != b.normalized.size())
                return a.normalized.size() < b.normalized.size();

            return a.normalized < b.normalized;
        });

    return cache;
}

bool CachedItemNames::Empty() const
{
    return entries_.empty();
}

std::size_t CachedItemNames::Size() const
{
    return entries_.size();
}

std::optional<MatchResult> CachedItemNames::FindBest(std::string_view input, int minConfidence) const
{
    const std::string normalizedInput = NormalizeName(input);

    if (normalizedInput.empty() || entries_.empty())
        return std::nullopt;

    const int inputLen = static_cast<int>(normalizedInput.size());
    const int slack = 100 - minConfidence;

    int minLen = inputLen - (inputLen * slack) / 100 - 1;
    int maxLen = minConfidence > 0 ? (inputLen * 100) / minConfidence + 2 : inputLen * 2 + 2;

    while (maxLen > inputLen && maxLen - (maxLen * slack) / 100 > inputLen)
        --maxLen;

    ++maxLen;
    minLen = std::max(1, minLen);

    const auto first = std::lower_bound(
        entries_.begin(),
        entries_.end(),
        static_cast<std::size_t>(minLen),
        [](const Entry& entry, std::size_t length)
        {
            return entry.normalized.size() < length;
        });

    const auto last = std::upper_bound(
        entries_.begin(),
        entries_.end(),
        static_cast<std::size_t>(maxLen),
        [](std::size_t length, const Entry& entry)
        {
            return length < entry.normalized.size();
        });

    const Histogram inputHistogram = MakeHistogram(normalizedInput);

    int bestScore = 0;
    const Entry* best = nullptr;

    for (auto it = first; it != last; ++it)
    {
        const int itemLen = static_cast<int>(it->normalized.size());
        const int maxLength = (std::max)(inputLen, itemLen);
        const int maxAllowedDistance = (maxLength * slack) / 100;

        if (std::abs(inputLen - itemLen) > maxAllowedDistance)
            continue;

        if (!HistogramAllows(inputHistogram, it->histogram, maxAllowedDistance))
            continue;

        const int score = SimilarityPercentNormalized(normalizedInput, it->normalized, minConfidence);

        if (score > bestScore)
        {
            bestScore = score;
            best = &*it;

            if (bestScore == 100)
                break;
        }
    }

    if (!best || bestScore < minConfidence)
        return std::nullopt;

    return MatchResult{ best->original, bestScore };
}
