#include "ConfigManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"
#include "platform/PlatformPaths.h"

using json = nlohmann::json;

namespace
{
constexpr std::string_view kDefaultPriceLeague = "Forbidden Rites";

bool IsSupportedPriceLeague(const std::string& league)
{
    return league == "Standard" ||
        league == "Hardcore" ||
        league == "Forbidden Rites" ||
        league == "HC Forbidden Rites" ||
        league == "Runes of Aldur" ||
        league == "HC Runes of Aldur";
}

void ClampPriceThresholds(AppConfig& config)
{
    config.priceColorMedium = std::max(0, config.priceColorMedium);
    config.priceColorHigh = std::max(config.priceColorMedium, config.priceColorHigh);
    config.priceColorVeryHigh = std::max(config.priceColorHigh, config.priceColorVeryHigh);
}
}

std::filesystem::path ConfigManager::GetConfigPath()
{
    return GetUserDataDir() / "config.json";
}

AppConfig& ConfigManager::Get()
{
    return config_;
}

const AppConfig& ConfigManager::Get() const
{
    return config_;
}

std::mutex& ConfigManager::Mutex() const
{
    return mutex_;
}

void ConfigManager::Normalize(AppConfig& config)
{
    config.regionW = std::max(0, config.regionW);
    config.regionH = std::max(0, config.regionH);
    config.ocrIntervalMs = std::clamp(config.ocrIntervalMs, 100, 2000);
    if (config.runeSearchScale < 0.5 || config.runeSearchScale > 2.0)
        config.runeSearchScale = 0.0;
    config.overlayFontSize = std::clamp(config.overlayFontSize, 8, 48);
    config.priceRefreshMinutes = std::clamp(config.priceRefreshMinutes, 5, 360);
    if (config.priceLeague == "Hardcore Runes of Aldur")
        config.priceLeague = "HC Runes of Aldur";
    if (!IsSupportedPriceLeague(config.priceLeague))
        config.priceLeague = std::string(kDefaultPriceLeague);
    ClampPriceThresholds(config);
}

bool ConfigManager::Load()
{
    std::ifstream file(GetConfigPath());

    if (!file)
        return false;

    json j = json::parse(file, nullptr, false);

    if (j.is_discarded())
        return false;

    config_.regionX = j.value("regionX", config_.regionX);
    config_.regionY = j.value("regionY", config_.regionY);
    config_.regionW = j.value("regionW", config_.regionW);
    config_.regionH = j.value("regionH", config_.regionH);

    config_.ocrEnabled      = j.value("ocrEnabled",     config_.ocrEnabled);
    config_.runeSearchEnabled  = j.value("runeSearchEnabled",  config_.runeSearchEnabled);
    config_.priceSearchEnabled = j.value("priceSearchEnabled", config_.priceSearchEnabled);
    config_.runeSearchScale    = j.value("runeSearchScale",    config_.runeSearchScale);
    config_.ocrIntervalMs   = j.value("ocrIntervalMs",  config_.ocrIntervalMs);

    config_.hotkeyToggleOCR         = j.value("hotkeyToggleOCR",        config_.hotkeyToggleOCR);
    config_.hotkeySingleSnapshot    = j.value("hotkeySingleSnapshot",   config_.hotkeySingleSnapshot);
    config_.hotkeySelectRegion      = j.value("hotkeySelectRegion",     config_.hotkeySelectRegion);

    config_.overlayOffsetX  = j.value("overlayOffsetX",  config_.overlayOffsetX);
    config_.overlayOffsetY  = j.value("overlayOffsetY",  config_.overlayOffsetY);
    config_.overlayFontSize = j.value("overlayFontSize", config_.overlayFontSize);

    config_.priceColorMedium    = j.value("priceColorMedium",   config_.priceColorMedium);
    config_.priceColorHigh      = j.value("priceColorHigh",     config_.priceColorHigh);
    config_.priceColorVeryHigh  = j.value("priceColorVeryHigh", config_.priceColorVeryHigh);

    config_.priceRefreshMinutes = j.value("priceRefreshMinutes",    config_.priceRefreshMinutes);
    config_.priceLeague         = j.value("priceLeague",            config_.priceLeague);

    config_.debugOCR    = j.value("debugOCR",       config_.debugOCR);

    Normalize(config_);

    return true;
}

bool ConfigManager::Save() const
{
    AppConfig config = config_;
    Normalize(config);

    json j;

    j["regionX"] = config.regionX;
    j["regionY"] = config.regionY;
    j["regionW"] = config.regionW;
    j["regionH"] = config.regionH;

    j["ocrEnabled"]     = config.ocrEnabled;
    j["runeSearchEnabled"]  = config.runeSearchEnabled;
    j["priceSearchEnabled"] = config.priceSearchEnabled;
    j["runeSearchScale"]    = config.runeSearchScale;
    j["ocrIntervalMs"]  = config.ocrIntervalMs;

    j["overlayOffsetX"]     = config.overlayOffsetX;
    j["overlayOffsetY"]     = config.overlayOffsetY;
    j["overlayFontSize"]    = config.overlayFontSize;

    j["hotkeyToggleOCR"]        = config.hotkeyToggleOCR;
    j["hotkeySingleSnapshot"]   = config.hotkeySingleSnapshot;
    j["hotkeySelectRegion"]     = config.hotkeySelectRegion;

    j["priceColorMedium"]       = config.priceColorMedium;
    j["priceColorHigh"]         = config.priceColorHigh;
    j["priceColorVeryHigh"]     = config.priceColorVeryHigh;

    j["priceRefreshMinutes"]    = config.priceRefreshMinutes;
    j["priceLeague"]            = config.priceLeague;

    j["debugOCR"]       = config.debugOCR;

    std::ofstream file(GetConfigPath());

    if (!file)
        return false;

    file << j.dump(4);
    return true;
}
