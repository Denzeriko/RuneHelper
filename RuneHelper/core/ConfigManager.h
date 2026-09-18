#pragma once

#include "Config.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

class ConfigManager
{
public:
    bool Load();
    bool Save() const;

    AppConfig Snapshot() const;
    void Update(const std::function<void(AppConfig&)>& change);

    static void Normalize(AppConfig& config);

    nlohmann::json FeatureSettings(const std::string& feature) const;
    void SetFeatureSettings(const std::string& feature, nlohmann::json settings);

private:
    AppConfig config_;
    nlohmann::json features_ = nlohmann::json::object();
    mutable std::mutex mutex_;

private:
    static std::filesystem::path GetConfigPath();
};
