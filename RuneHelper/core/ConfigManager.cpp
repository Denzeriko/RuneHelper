#include "ConfigManager.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"
#include "core/AtomicFile.h"
#include "core/JsonRead.h"
#include "core/Logger.h"
#include "core/Text.h"
#include "platform/PlatformPaths.h"

using json = nlohmann::json;

namespace
{
constexpr std::size_t kMaxPriceLeagueLength = 64;
constexpr std::chrono::milliseconds kSaveDelay{ 500 };

struct LeagueRename
{
    std::string_view from;
    std::string_view to;
};

constexpr LeagueRename kRenamedLeagues[] = {
    { "Hardcore Runes of Aldur", "HC Runes of Aldur" },
};

void SetAsideUnreadableConfig(const std::filesystem::path& path)
{
    std::filesystem::path aside = path;
    aside += ".bad";

    std::error_code ec;
    std::filesystem::remove(aside, ec);
    std::filesystem::rename(path, aside, ec);

    if (ec)
        LOG_ERROR("config.json is not a readable JSON object and could not be moved aside, defaults are used: " + ec.message());
    else
        LOG_ERROR("config.json is not a readable JSON object, it was kept as config.json.bad and defaults are used");
}

std::string SanitizePriceLeague(std::string_view league)
{
    std::string printable;
    printable.reserve(league.size());

    for (unsigned char ch : league)
    {
        if (ch >= 0x20 && ch != 0x7f)
            printable.push_back(static_cast<char>(ch));
    }

    std::string cleaned(Trim(printable));

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
    std::lock_guard<std::mutex> lock(mutex_);

    const AppConfig before = config_;
    change(config_);
    Normalize(config_);

    if (config_ != before)
        MarkChanged();
}

void ConfigManager::MarkChanged()
{
    changed_ = true;
    changedAt_ = std::chrono::steady_clock::now();
}

void ConfigManager::SaveIfSettled()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!changed_ || std::chrono::steady_clock::now() - changedAt_ < kSaveDelay)
            return;
    }

    Flush();
}

void ConfigManager::Flush()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!changed_)
            return;

        changed_ = false;
    }

    if (!Save())
        LOG_ERROR("config.json could not be saved");
}

void ConfigManager::Normalize(AppConfig& config)
{
    config.regionW = std::max(0, config.regionW);
    config.regionH = std::max(0, config.regionH);
    config.overlayFontSize = std::clamp(config.overlayFontSize, kMinOverlayFontSize, kMaxOverlayFontSize);
    config.priceRefreshMinutes = std::clamp(config.priceRefreshMinutes, kMinPriceRefreshMinutes, kMaxPriceRefreshMinutes);

    for (const LeagueRename& rename : kRenamedLeagues)
    {
        if (config.priceLeague == rename.from)
            config.priceLeague = std::string(rename.to);
    }

    config.priceLeague = SanitizePriceLeague(config.priceLeague);

    if (config.priceLeague.empty())
        config.priceLeague = std::string(kDefaultPriceLeague);
    ClampPriceThresholds(config);

    const bool knownLanguage = std::any_of(
        kGameLanguages.begin(),
        kGameLanguages.end(),
        [&config](const GameLanguage& language) { return language.code == config.gameLanguage; }
    );

    if (!knownLanguage)
        config.gameLanguage = std::string(kGameLanguages.front().code);
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

    nlohmann::json& stored = features_[feature];

    if (stored == settings)
        return;

    stored = std::move(settings);
    MarkChanged();
}

bool ConfigManager::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);

    const std::filesystem::path path = GetConfigPath();
    std::ifstream file(path);

    if (!file)
        return false;

    json j = json::parse(file, nullptr, false);
    file.close();

    if (j.is_discarded() || !j.is_object())
    {
        SetAsideUnreadableConfig(path);
        return false;
    }

    if (const auto features = j.find("features"); features != j.end() && features->is_object())
        features_ = *features;

    config_.regionX = JsonValue(j, "regionX", config_.regionX);
    config_.regionY = JsonValue(j, "regionY", config_.regionY);
    config_.regionW = JsonValue(j, "regionW", config_.regionW);
    config_.regionH = JsonValue(j, "regionH", config_.regionH);

    config_.ocrEnabled = JsonValue(j, "ocrEnabled", config_.ocrEnabled);
    config_.overlayBackground = JsonValue(j, "overlayBackground", config_.overlayBackground);
    config_.overlayOutline = JsonValue(j, "overlayOutline", config_.overlayOutline);
    config_.overlayIcons = JsonValue(j, "overlayIcons", config_.overlayIcons);
    config_.priceSearchEnabled = JsonValue(j, "priceSearchEnabled", config_.priceSearchEnabled);

    config_.hotkeyToggleOCR = JsonValue(j, "hotkeyToggleOCR", config_.hotkeyToggleOCR);
    config_.hotkeySingleSnapshot = JsonValue(j, "hotkeySingleSnapshot", config_.hotkeySingleSnapshot);
    config_.hotkeySelectRegion = JsonValue(j, "hotkeySelectRegion", config_.hotkeySelectRegion);

    config_.overlayOffsetX = JsonValue(j, "overlayOffsetX", config_.overlayOffsetX);
    config_.overlayOffsetY = JsonValue(j, "overlayOffsetY", config_.overlayOffsetY);
    config_.overlayFontSize = JsonValue(j, "overlayFontSize", config_.overlayFontSize);

    config_.priceUnit = PriceUnitFromInt(JsonValue(j, "priceUnit", static_cast<int>(config_.priceUnit)));

    config_.priceColorMedium = JsonValue(j, "priceColorMedium", config_.priceColorMedium);
    config_.priceColorHigh = JsonValue(j, "priceColorHigh", config_.priceColorHigh);
    config_.priceColorVeryHigh = JsonValue(j, "priceColorVeryHigh", config_.priceColorVeryHigh);

    config_.priceRefreshMinutes = JsonValue(j, "priceRefreshMinutes", config_.priceRefreshMinutes);
    config_.priceLeague = JsonValue(j, "priceLeague", config_.priceLeague);
    config_.gameLanguage = JsonValue(j, "gameLanguage", config_.gameLanguage);

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

    j["regionX"] = config.regionX;
    j["regionY"] = config.regionY;
    j["regionW"] = config.regionW;
    j["regionH"] = config.regionH;

    j["ocrEnabled"] = config.ocrEnabled;
    j["overlayBackground"] = config.overlayBackground;
    j["overlayOutline"] = config.overlayOutline;
    j["overlayIcons"] = config.overlayIcons;
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
    j["gameLanguage"] = config.gameLanguage;

    return WriteFileAtomic(GetConfigPath(), j.dump(4));
}
