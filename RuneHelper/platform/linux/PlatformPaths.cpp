#include "platform/PlatformPaths.h"
#include "core/Logger.h"

#include <cstdlib>
#include <filesystem>

const std::filesystem::path& GetUserDataDir()
{
    static const std::filesystem::path dir = []
    {
        const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");

        std::filesystem::path result;

        if (xdgConfigHome && *xdgConfigHome)
        {
            result = xdgConfigHome;
        }
        else if (const char* home = std::getenv("HOME"); home && *home)
        {
            result = std::filesystem::path(home) / ".config";
        }
        else
        {
            result = ".";
        }

        result /= "RuneHelper";

        std::error_code ec;
        std::filesystem::create_directories(result, ec);

        if (ec)
            LOG_ERROR("GetUserDataDir() -> create_directories failed: " + ec.message());

        LOG_INFO("GetUserDataDir() -> " + result.string());

        return result;
    }();

    return dir;
}
