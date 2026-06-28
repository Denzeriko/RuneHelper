#pragma once

#include "Config.h"
#include <filesystem>

class ConfigManager
{
public:
    bool Load();
    bool Save() const;

    AppConfig& Get();
    const AppConfig& Get() const;

private:
    AppConfig config_;

private:
    static std::filesystem::path GetConfigPath();
};