#include "platform/PlatformPaths.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>

#include "core/Logger.h"

const std::filesystem::path& GetUserDataDir()
{
    static const std::filesystem::path dir = []
    {
        PWSTR path = nullptr;

        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
        {
            LOG_ERROR("GetUserDataDir() -> SHGetKnownFolderPath failed");
            return std::filesystem::path(".");
        }

        std::filesystem::path result(path);
        CoTaskMemFree(path);

        result /= "Denz";
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
