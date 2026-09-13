#pragma once

#include "Config.h"

#include <filesystem>
#include <functional>
#include <mutex>

class ConfigManager
{
public:
    bool Load();
    bool Save() const;

    AppConfig Snapshot() const;
    void Update(const std::function<void(AppConfig&)>& change);

    static void Normalize(AppConfig& config);

private:
    AppConfig config_;
    mutable std::mutex mutex_;

private:
    static std::filesystem::path GetConfigPath();
};
