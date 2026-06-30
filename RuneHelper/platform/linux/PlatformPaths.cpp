#include "platform/PlatformPaths.h"
#include "core/Logger.h"

#include <cstdlib>
#include <filesystem>

std::filesystem::path GetAppDataDir()
{
    const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME");

    std::filesystem::path result;

    if (xdgConfigHome && *xdgConfigHome)
    {
        result = xdgConfigHome;
    }
    else if (const char *home = std::getenv("HOME"); home && *home)
    {
        result = std::filesystem::path(home) / ".config";
    }
    else
    {
        result = ".";
    }

    result /= "RuneHelper";

    if (!std::filesystem::exists(result))
    {
        std::filesystem::create_directories(result);
    }

    LOG_INFO("GetAppDataDir() -> return -> " + result.string());

    return result;
}
