#pragma once

#include <string>
#include <filesystem>

#include "ConfigManager.h"
#include "platform/PlatformPaths.h"
#include "ui/OverlayState.h"

std::string ExtractItemName(const std::string& line);
std::wstring ToWide(const std::string& s);
OverlayColor GetPriceColor(double priceEx, AppConfig &config);
std::string VkToString(int vk);
