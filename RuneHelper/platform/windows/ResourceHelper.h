#pragma once
#include <string>
#include <filesystem>
#include <windows.h>

bool ExtractResourceToFile(int resId, LPCWSTR resType, const std::filesystem::path& outPath);
std::string PrepareTessdata();