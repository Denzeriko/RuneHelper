#include "NameNormalizer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr size_t kMaxLen = 128;
constexpr char32_t kReplacement = 0xFFFD;

constexpr std::array<char, 32> kLatin1Letters = { 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c',  'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                                                  'd', 'n', 'o', 'o', 'o', 'o', 'o', '\0', 'o', 'u', 'u', 'u', 'u', 'y', 't', 'y' };

char32_t DecodeUtf8(std::string_view text, std::size_t& i)
{
    const unsigned char lead = static_cast<unsigned char>(text[i++]);

    if (lead < 0x80)
        return lead;

    int extra = 0;
    char32_t codePoint = 0;

    if ((lead & 0xE0) == 0xC0)
    {
        extra = 1;
        codePoint = lead & 0x1F;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        extra = 2;
        codePoint = lead & 0x0F;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        extra = 3;
        codePoint = lead & 0x07;
    }
    else
    {
        return kReplacement;
    }

    for (int k = 0; k < extra; ++k)
    {
        if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            return kReplacement;

        codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[i++]) & 0x3F);
    }

    return codePoint;
}

void AppendUtf8(std::string& out, char32_t codePoint)
{
    if (codePoint < 0x80)
    {
        out.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

char32_t FoldLetter(char32_t c)
{
    if (c >= 0xFF01 && c <= 0xFF5E)
        c -= 0xFEE0;

    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';

    if (c < 0x80)
        return c;

    if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
        c += 0x20;

    if (c == 0xDF)
        return 's';

    if (c >= 0xE0 && c <= 0xFF)
        return static_cast<unsigned char>(kLatin1Letters[c - 0xE0]);

    if (c == 0x152 || c == 0x153)
        return 'o';

    if (c >= 0x400 && c <= 0x40F)
        c += 0x50;
    else if (c >= 0x410 && c <= 0x42F)
        c += 0x20;

    if (c == 0x451)
        return 0x435;

    return c;
}

bool IsSeparator(char32_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 || c == 0x3000;
}

bool IsWordCharacter(char32_t c)
{
    if (c == 0 || c == kReplacement)
        return false;

    if (c < 0x80)
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');

    if (c < 0xC0 || c == 0xD7 || c == 0xF7)
        return false;

    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F))
        return false;

    return !((c >= 0xFE10 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF65));
}

std::u32string NormalizeCodePoints(std::string_view s)
{
    std::u32string out;
    out.reserve(s.size());

    bool prevSpace = true;
    std::size_t i = 0;

    while (i < s.size())
    {
        const char32_t raw = DecodeUtf8(s, i);

        if (IsSeparator(raw))
        {
            if (!prevSpace)
            {
                out.push_back(' ');
                prevSpace = true;
            }

            continue;
        }

        const char32_t folded = FoldLetter(raw);

        if (IsWordCharacter(folded))
        {
            out.push_back(folded);
            prevSpace = false;
        }
    }

    if (!out.empty() && out.back() == ' ')
        out.pop_back();

    return out;
}

int BoundedLevenshteinDistance(std::u32string_view a, std::u32string_view b, int maxDistance)
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
        const char32_t ca = a[i - 1];

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

CachedItemNames::Histogram MakeHistogram(std::u32string_view text)
{
    CachedItemNames::Histogram histogram{};

    for (const char32_t c : text)
    {
        std::size_t bucket = 36 + static_cast<std::size_t>(c % 32);

        if (c >= 'a' && c <= 'z')
            bucket = static_cast<std::size_t>(c - 'a');
        else if (c >= '0' && c <= '9')
            bucket = 26 + static_cast<std::size_t>(c - '0');

        if (histogram[bucket] < 255)
            ++histogram[bucket];
    }

    return histogram;
}

bool HistogramAllows(const CachedItemNames::Histogram& a, const CachedItemNames::Histogram& b, int maxDistance)
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

int SimilarityPercentNormalized(std::u32string_view a, std::u32string_view b, int minConfidence)
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

    for (const char32_t c : NormalizeCodePoints(s))
        AppendUtf8(out, c);

    return out;
}

CachedItemNames CachedItemNames::Build(const std::vector<std::string>& names)
{
    std::vector<NameAlias> aliases;
    aliases.reserve(names.size());

    for (const auto& name : names)
        aliases.push_back({ name, name });

    return Build(aliases);
}

CachedItemNames CachedItemNames::Build(const std::vector<NameAlias>& aliases)
{
    CachedItemNames cache;
    cache.entries_.reserve(aliases.size());

    for (const auto& [name, alias] : aliases)
    {
        std::u32string normalized = NormalizeCodePoints(alias);

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
        }
    );

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
    const std::u32string normalizedInput = NormalizeCodePoints(input);

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
        [](const Entry& entry, std::size_t length) { return entry.normalized.size() < length; }
    );

    const auto last = std::upper_bound(
        entries_.begin(),
        entries_.end(),
        static_cast<std::size_t>(maxLen),
        [](std::size_t length, const Entry& entry) { return length < entry.normalized.size(); }
    );

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
