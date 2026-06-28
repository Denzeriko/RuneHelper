#include <optional>
#include <string>
#include <vector>

std::string NormalizeName(std::string s);
double Similarity(const std::string& a, const std::string& b);
std::optional<std::string> FindBestItemMatch(const std::string& ocrText, const std::vector<std::string>& dbNames);\