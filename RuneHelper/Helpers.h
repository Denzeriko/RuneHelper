#pragma once
#include <string>
#include <filesystem>

std::string ExtractItemName(const std::string& line);
std::wstring ToWide(const std::string& s);
std::filesystem::path GetAppDataDir();