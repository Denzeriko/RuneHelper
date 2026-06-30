#include "platform/PlatformPaths.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>

#include "core/Logger.h"

std::filesystem::path GetAppDataDir()
{
    LOG_INFO("GetAppDataDir() -> call");
    PWSTR path = nullptr;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
    {
        LOG_ERROR("GetAppDataDir() -> SHGetKnownFolderPath -> ERROR");
        return ".";
    }

    std::filesystem::path result(path);
    CoTaskMemFree(path);

    result /= "Denz";
    result /= "RuneHelper";

    if(!std::filesystem::exists(result))
    {
        std::filesystem::create_directories(result);
    }

    LOG_INFO("GetAppDataDir() -> return -> " + result.string());

    return result;
}
