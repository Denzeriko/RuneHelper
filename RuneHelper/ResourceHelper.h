#pragma once
#include <string>
#include <filesystem>

bool ExtractResourceToFile(int resId, const wchar_t* resType, const std::filesystem::path& outPath);
std::string PrepareTessdata();