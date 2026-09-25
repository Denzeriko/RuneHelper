#pragma once

#include "Config.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

class ConfigManager
{
public:
    bool Load();

    AppConfig Snapshot() const;
    void Update(const std::function<void(AppConfig&)>& change);

    nlohmann::json FeatureSettings(const std::string& feature) const;
    void SetFeatureSettings(const std::string& feature, nlohmann::json settings);

    void SaveIfSettled();
    void Flush();

    static void Normalize(AppConfig& config);

private:
    bool Save() const;
    void MarkChanged();

    static std::filesystem::path GetConfigPath();

    AppConfig config_;
    nlohmann::json features_ = nlohmann::json::object();
    bool changed_ = false;
    std::chrono::steady_clock::time_point changedAt_{};
    mutable std::mutex mutex_;
};
