#include <optional>
#include <string>
#include <vector>

struct MatchResult
{
    std::string name;
    int confidence;
};

std::string NormalizeName(std::string s);
double Similarity(const std::string& a, const std::string& b);
std::optional<MatchResult> FindBestItemMatch(const std::string& input, const std::vector<std::string>& candidates);