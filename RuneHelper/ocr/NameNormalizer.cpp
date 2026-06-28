#include "NameNormalizer.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <optional>

std::string NormalizeName(std::string s)
{
    std::string out;

    for (char c : s)
    {
        if (std::isalnum((unsigned char)c) || std::isspace((unsigned char)c))
            out += (char)std::tolower((unsigned char)c);
    }

    std::string compact;
    bool prevSpace = false;

    for (char c : out)
    {
        if (std::isspace((unsigned char)c))
        {
            if (!prevSpace)
                compact += ' ';
            prevSpace = true;
        }
        else
        {
            compact += c;
            prevSpace = false;
        }
    }

    while (!compact.empty() && compact.front() == ' ')
        compact.erase(compact.begin());

    while (!compact.empty() && compact.back() == ' ')
        compact.pop_back();

    return compact;
}

int LevenshteinDistance(const std::string& a, const std::string& b)
{
    std::vector<int> prev(b.size() + 1);
    std::vector<int> cur(b.size() + 1);

    for (int j = 0; j <= (int)b.size(); ++j)
        prev[j] = j;

    for (int i = 1; i <= (int)a.size(); ++i)
    {
        cur[0] = i;
        for (int j = 1; j <= (int)b.size(); ++j)
        {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }

    return prev[b.size()];
}

double Similarity(const std::string& a, const std::string& b)
{
    std::string na = NormalizeName(a);
    std::string nb = NormalizeName(b);

    if (na.empty() || nb.empty())
        return 0.0;

    int dist = LevenshteinDistance(na, nb);
    int maxLen = std::max(na.size(), nb.size());

    return 1.0 - ((double)dist / (double)maxLen);
}

std::optional<std::string> FindBestItemMatch(const std::string& ocrText, const std::vector<std::string>& dbNames)
{
    std::string bestName;
    double bestScore = 0.0;

    for (const auto& name : dbNames)
    {
        double score = Similarity(ocrText, name);

        if (score > bestScore)
        {
            bestScore = score;
            bestName = name;
        }
    }

    // 0.70-0.80
    if (bestScore >= 0.72)
        return bestName;

    return std::nullopt;
}