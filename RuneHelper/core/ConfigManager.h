#pragma once

#include "Config.h"
#include <filesystem>
#include <mutex>

class ConfigManager
{
public:
    bool Load();
    bool Save() const;

    AppConfig& Get();
    const AppConfig& Get() const;
    std::mutex& Mutex() const;
    static void Normalize(AppConfig& config);

private:
    AppConfig config_;
    mutable std::mutex mutex_;

private:
    static std::filesystem::path GetConfigPath();
};
