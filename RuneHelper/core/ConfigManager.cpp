#include "ConfigManager.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"
#include "core/AtomicFile.h"
#include "platform/PlatformPaths.h"

using json = nlohmann::json;

namespace
{
constexpr std::string_view kDefaultPriceLeague = "Forbidden Rites";
constexpr std::size_t kMaxPriceLeagueLength = 64;

std::string SanitizePriceLeague(std::string_view league)
{
    std::string cleaned;
    cleaned.reserve(league.size());

    for (unsigned char ch : league)
    {
        if (ch >= 0x20 && ch != 0x7f)
            cleaned.push_back(static_cast<char>(ch));
    }

    const std::size_t first = cleaned.find_first_not_of(' ');

    if (first == std::string::npos)
        return {};

    const std::size_t last = cleaned.find_last_not_of(' ');
    cleaned = cleaned.substr(first, last - first + 1);

    if (cleaned.size() > kMaxPriceLeagueLength)
        cleaned.resize(kMaxPriceLeagueLength);

    return cleaned;
}

PriceUnit PriceUnitFromInt(int value)
{
    if (value < static_cast<int>(PriceUnit::Exalted) || value > static_cast<int>(PriceUnit::Divine))
        return PriceUnit::ExaltedWithDivine;

    return static_cast<PriceUnit>(value);
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

AppConfig ConfigManager::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void ConfigManager::Update(const std::function<void(AppConfig&)>& change)
{
    if (!change)
        return;

    std::lock_guard<std::mutex> lock(mutex_);

    change(config_);
    Normalize(config_);
}

void ConfigManager::Normalize(AppConfig& config)
{
    config.regionW = std::max(0, config.regionW);
    config.regionH = std::max(0, config.regionH);
    config.overlayFontSize = std::clamp(config.overlayFontSize, 8, 48);
    config.priceRefreshMinutes = std::clamp(config.priceRefreshMinutes, 5, 360);
    if (config.priceLeague == "Hardcore Runes of Aldur")
        config.priceLeague = "HC Runes of Aldur";

    config.priceLeague = SanitizePriceLeague(config.priceLeague);

    if (config.priceLeague.empty())
        config.priceLeague = std::string(kDefaultPriceLeague);
    ClampPriceThresholds(config);
}

nlohmann::json ConfigManager::FeatureSettings(const std::string& feature) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = features_.find(feature);

    if (it == features_.end() || !it->is_object())
        return nlohmann::json::object();

    return *it;
}

void ConfigManager::SetFeatureSettings(const std::string& feature, nlohmann::json settings)
{
    std::lock_guard<std::mutex> lock(mutex_);
    features_[feature] = std::move(settings);
}

bool ConfigManager::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream file(GetConfigPath());

    if (!file)
        return false;

    json j = json::parse(file, nullptr, false);

    if (j.is_discarded())
        return false;

    if (j.contains("features") && j["features"].is_object())
        features_ = j["features"];

    config_.regionX = j.value("regionX", config_.regionX);
    config_.regionY = j.value("regionY", config_.regionY);
    config_.regionW = j.value("regionW", config_.regionW);
    config_.regionH = j.value("regionH", config_.regionH);

    config_.ocrEnabled = j.value("ocrEnabled", config_.ocrEnabled);
    config_.overlayBackground = j.value("overlayBackground", config_.overlayBackground);
    config_.overlayOutline = j.value("overlayOutline", config_.overlayOutline);
    config_.priceSearchEnabled = j.value("priceSearchEnabled", config_.priceSearchEnabled);

    config_.hotkeyToggleOCR = j.value("hotkeyToggleOCR", config_.hotkeyToggleOCR);
    config_.hotkeySingleSnapshot = j.value("hotkeySingleSnapshot", config_.hotkeySingleSnapshot);
    config_.hotkeySelectRegion = j.value("hotkeySelectRegion", config_.hotkeySelectRegion);

    config_.overlayOffsetX = j.value("overlayOffsetX", config_.overlayOffsetX);
    config_.overlayOffsetY = j.value("overlayOffsetY", config_.overlayOffsetY);
    config_.overlayFontSize = j.value("overlayFontSize", config_.overlayFontSize);

    config_.priceUnit = PriceUnitFromInt(j.value("priceUnit", static_cast<int>(config_.priceUnit)));

    config_.priceColorMedium = j.value("priceColorMedium", config_.priceColorMedium);
    config_.priceColorHigh = j.value("priceColorHigh", config_.priceColorHigh);
    config_.priceColorVeryHigh = j.value("priceColorVeryHigh", config_.priceColorVeryHigh);

    config_.priceRefreshMinutes = j.value("priceRefreshMinutes", config_.priceRefreshMinutes);
    config_.priceLeague = j.value("priceLeague", config_.priceLeague);

    config_.debugOCR = j.value("debugOCR", config_.debugOCR);

    Normalize(config_);

    return true;
}

bool ConfigManager::Save() const
{
    AppConfig config;
    json j;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        config = config_;

        if (!features_.empty())
            j["features"] = features_;
    }

    Normalize(config);

    j["regionX"] = config.regionX;
    j["regionY"] = config.regionY;
    j["regionW"] = config.regionW;
    j["regionH"] = config.regionH;

    j["ocrEnabled"] = config.ocrEnabled;
    j["overlayBackground"] = config.overlayBackground;
    j["overlayOutline"] = config.overlayOutline;
    j["priceSearchEnabled"] = config.priceSearchEnabled;

    j["overlayOffsetX"] = config.overlayOffsetX;
    j["overlayOffsetY"] = config.overlayOffsetY;
    j["overlayFontSize"] = config.overlayFontSize;

    j["hotkeyToggleOCR"] = config.hotkeyToggleOCR;
    j["hotkeySingleSnapshot"] = config.hotkeySingleSnapshot;
    j["hotkeySelectRegion"] = config.hotkeySelectRegion;

    j["priceUnit"] = static_cast<int>(config.priceUnit);
    j["priceColorMedium"] = config.priceColorMedium;
    j["priceColorHigh"] = config.priceColorHigh;
    j["priceColorVeryHigh"] = config.priceColorVeryHigh;

    j["priceRefreshMinutes"] = config.priceRefreshMinutes;
    j["priceLeague"] = config.priceLeague;

    j["debugOCR"] = config.debugOCR;

    return WriteFileAtomic(GetConfigPath(), j.dump(4));
}
