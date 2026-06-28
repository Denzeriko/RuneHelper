#pragma once
#include <windows.h>
#include <string>
#include <filesystem>
#include "ConfigManager.h"

std::string ExtractItemName(const std::string& line);
std::wstring ToWide(const std::string& s);
std::filesystem::path GetAppDataDir();
COLORREF GetPriceColor(double priceEx, AppConfig &config);