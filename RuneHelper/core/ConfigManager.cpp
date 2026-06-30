#include "ConfigManager.h"

#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"
#include "Helpers.h"

using json = nlohmann::json;

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
    config_.priceProvider       = j.value("priceProvider",          config_.priceProvider);

    config_.debugOCR    = j.value("debugOCR",       config_.debugOCR);
    config_.showConsole = j.value("showConsole",    config_.showConsole);

    return true;
}

bool ConfigManager::Save() const
{
    json j;

    j["regionX"] = config_.regionX;
    j["regionY"] = config_.regionY;
    j["regionW"] = config_.regionW;
    j["regionH"] = config_.regionH;

    j["ocrEnabled"]     = config_.ocrEnabled;
    j["ocrAutoDetect"]  = config_.ocrAutoDetect;
    j["ocrScale"]       = config_.ocrScale;
    j["ocrThreshold"]   = config_.ocrThreshold;
    j["ocrPasses"]      = config_.ocrPasses;
    j["ocrIntervalMs"]  = config_.ocrIntervalMs;

    j["overlayOffsetX"]     = config_.overlayOffsetX;
    j["overlayOffsetY"]     = config_.overlayOffsetY;
    j["overlayFontSize"]    = config_.overlayFontSize;

    j["hotkeyToggleOCR"]        = config_.hotkeyToggleOCR;
    j["hotkeySingleSnapshot"]   = config_.hotkeySingleSnapshot;
    j["hotkeySelectRegion"]     = config_.hotkeySelectRegion;

    j["priceColorMedium"]       = config_.priceColorMedium;
    j["hotkeySingleSnapshot"]   = config_.hotkeySingleSnapshot;
    j["hotkeySelectRegion"]     = config_.hotkeySelectRegion;

    j["priceRefreshMinutes"]    = config_.priceRefreshMinutes;
    j["priceProvider"]          = config_.priceProvider;

    j["debugOCR"]       = config_.debugOCR;
    j["showConsole"]    = config_.showConsole;

    std::ofstream file(GetConfigPath());

    if (!file)
        return false;

    file << j.dump(4);
    return true;
}
