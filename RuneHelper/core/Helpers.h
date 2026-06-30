#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include "platform/linux/PlatformTypes.h"
#endif
#include <string>
#include <filesystem>
#include "ConfigManager.h"
#include "platform/PlatformPaths.h"

std::string ExtractItemName(const std::string& line);
std::wstring ToWide(const std::string& s);
COLORREF GetPriceColor(double priceEx, AppConfig &config);
std::string VkToString(int vk);
