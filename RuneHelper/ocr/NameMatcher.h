#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct MatchResult
{
    std::string name;
    int confidence = 0;
};

struct NameAlias
{
    std::string name;
    std::string alias;
};

class NameMatcher
{
public:
    static NameMatcher Build(const std::vector<std::string>& names);
    static NameMatcher Build(const std::vector<NameAlias>& aliases);

    bool Empty() const;
    std::size_t Size() const;

    std::optional<MatchResult> FindBest(std::string_view input, int minConfidence = 82) const;

public:
    static constexpr std::size_t kHistogramSize = 68;
    using Histogram = std::array<std::uint8_t, kHistogramSize>;

private:
    struct Entry
    {
        std::string original;
        std::u32string normalized;
        Histogram histogram{};
    };

    std::vector<Entry> entries_;
};

std::string NormalizeName(std::string_view s);
