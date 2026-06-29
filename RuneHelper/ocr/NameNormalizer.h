#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct MatchResult
{
    std::string name;
    int confidence = 0;
};

struct CachedItemName
{
    std::string original;
    std::string normalized;
};

std::string NormalizeName(std::string_view s);
std::vector<CachedItemName> BuildCachedItemNames(const std::vector<std::string>& names);
std::optional<MatchResult> FindBestItemMatch(std::string_view input, const std::vector<CachedItemName>& candidates, int minConfidence = 72);