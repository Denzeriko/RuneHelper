#include "ocr/NameMatcher.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/Text.h"

namespace
{
constexpr size_t kMaxLen = 128;

constexpr std::size_t kLetterBuckets = 26;
constexpr std::size_t kDigitBuckets = 10;
constexpr std::size_t kOtherBuckets = 32;

static_assert(NameMatcher::kHistogramSize == kLetterBuckets + kDigitBuckets + kOtherBuckets);

constexpr std::array<char, 32> kLatin1Letters = { 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c',  'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                                                  'd', 'n', 'o', 'o', 'o', 'o', 'o', '\0', 'o', 'u', 'u', 'u', 'u', 'y', 't', 'y' };

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
    if (c == 0 || c == kReplacementCharacter)
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

NameMatcher::Histogram MakeHistogram(std::u32string_view text)
{
    NameMatcher::Histogram histogram{};

    for (const char32_t c : text)
    {
        std::size_t bucket = kLetterBuckets + kDigitBuckets + static_cast<std::size_t>(c % kOtherBuckets);

        if (c >= 'a' && c <= 'z')
            bucket = static_cast<std::size_t>(c - 'a');
        else if (c >= '0' && c <= '9')
            bucket = kLetterBuckets + static_cast<std::size_t>(c - '0');

        if (histogram[bucket] < 255)
            ++histogram[bucket];
    }

    return histogram;
}

bool HistogramAllows(const NameMatcher::Histogram& a, const NameMatcher::Histogram& b, int maxDistance)
{
    const int limit = 2 * maxDistance;
    int difference = 0;

    for (std::size_t i = 0; i < NameMatcher::kHistogramSize; ++i)
    {
        difference += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));

        if (difference > limit)
            return false;
    }

    return true;
}

struct LengthRange
{
    int shortest = 0;
    int longest = 0;
};

LengthRange CandidateLengths(int inputLength, int minConfidence)
{
    const int slack = 100 - minConfidence;
    int longest = minConfidence > 0 ? (inputLength * 100) / minConfidence + 2 : inputLength * 2 + 2;

    while (longest > inputLength && longest - (longest * slack) / 100 > inputLength)
        --longest;

    return { std::max(1, inputLength - (inputLength * slack) / 100 - 1), longest + 1 };
}

int SimilarityPercent(std::u32string_view a, std::u32string_view b, int minConfidence)
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

NameMatcher NameMatcher::Build(const std::vector<std::string>& names)
{
    std::vector<NameAlias> aliases;
    aliases.reserve(names.size());

    for (const auto& name : names)
        aliases.push_back({ name, name });

    return Build(aliases);
}

NameMatcher NameMatcher::Build(const std::vector<NameAlias>& aliases)
{
    NameMatcher matcher;
    matcher.entries_.reserve(aliases.size());

    for (const auto& [name, alias] : aliases)
    {
        std::u32string normalized = NormalizeCodePoints(alias);

        if (normalized.empty())
            continue;

        Histogram histogram = MakeHistogram(normalized);

        matcher.entries_.push_back({ name, std::move(normalized), histogram });
    }

    std::sort(
        matcher.entries_.begin(),
        matcher.entries_.end(),
        [](const Entry& a, const Entry& b)
        {
            if (a.normalized.size() != b.normalized.size())
                return a.normalized.size() < b.normalized.size();

            return a.normalized < b.normalized;
        }
    );

    return matcher;
}

bool NameMatcher::Empty() const
{
    return entries_.empty();
}

std::size_t NameMatcher::Size() const
{
    return entries_.size();
}

std::optional<MatchResult> NameMatcher::FindBest(std::string_view input, int minConfidence) const
{
    const std::u32string normalizedInput = NormalizeCodePoints(input);

    if (normalizedInput.empty() || entries_.empty())
        return std::nullopt;

    const int inputLen = static_cast<int>(normalizedInput.size());
    const int slack = 100 - minConfidence;
    const LengthRange lengths = CandidateLengths(inputLen, minConfidence);

    const auto first = std::lower_bound(
        entries_.begin(),
        entries_.end(),
        static_cast<std::size_t>(lengths.shortest),
        [](const Entry& entry, std::size_t length) { return entry.normalized.size() < length; }
    );

    const auto last = std::upper_bound(
        entries_.begin(),
        entries_.end(),
        static_cast<std::size_t>(lengths.longest),
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

        const int score = SimilarityPercent(normalizedInput, it->normalized, minConfidence);

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
