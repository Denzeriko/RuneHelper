#include "ConfigManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"
#include "Helpers.h"

using json = nlohmann::json;

namespace
{
constexpr std::string_view kDefaultPriceLeague = "Runes of Aldur";

bool IsSupportedPriceLeague(const std::string& league)
{
    return league == "Standard" ||
        league == "Hardcore" ||
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
    return GetAppDataDir() / "config.json";
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
    config.ocrScale = std::clamp(config.ocrScale, 0.5f, 4.0f);
    config.ocrThreshold = std::clamp(config.ocrThreshold, 0.0f, 255.0f);
    config.ocrPasses = std::clamp(config.ocrPasses, 1, 6);
    config.ocrIntervalMs = std::clamp(config.ocrIntervalMs, 100, 2000);
    config.overlayFontSize = std::clamp(config.overlayFontSize, 8, 48);
    config.priceRefreshMinutes = std::clamp(config.priceRefreshMinutes, 1, 60);
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
    config_.ocrAutoDetect   = j.value("ocrAutoDetect",  config_.ocrAutoDetect);
    config_.ocrScale        = j.value("ocrScale",       config_.ocrScale);
    config_.ocrThreshold    = j.value("ocrThreshold",   config_.ocrThreshold);
    config_.ocrPasses       = j.value("ocrPasses",      config_.ocrPasses);
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
    config_.priceProvider       = j.value("priceProvider",          config_.priceProvider);

    config_.debugOCR    = j.value("debugOCR",       config_.debugOCR);
    config_.showConsole = j.value("showConsole",    config_.showConsole);

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
    j["ocrAutoDetect"]  = config.ocrAutoDetect;
    j["ocrScale"]       = config.ocrScale;
    j["ocrThreshold"]   = config.ocrThreshold;
    j["ocrPasses"]      = config.ocrPasses;
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
    j["priceProvider"]          = config.priceProvider;

    j["debugOCR"]       = config.debugOCR;
    j["showConsole"]    = config.showConsole;

    std::ofstream file(GetConfigPath());

    if (!file)
        return false;

    file << j.dump(4);
    return true;
}
