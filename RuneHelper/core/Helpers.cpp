#include "Helpers.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include <cstdlib>
#include <regex>
#include <string>

#include "Logger.h"
#include "Config.h"

std::string ExtractItemName(const std::string& line)
{
    std::regex re(R"(^\s*\d+x\s+(.+)$)");
    std::smatch m;

    if (std::regex_match(line, m, re))
        return m[1].str();

    return line;
}

std::wstring ToWide(const std::string& s)
{
    if (s.empty())
        return L"";

#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);

    if (size <= 0)
        return L"";

    std::wstring result(size - 1, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);

    return result;
#else
    return std::wstring(s.begin(), s.end());
#endif
}


std::filesystem::path GetAppDataDir() 
{
    LOG_INFO("GetAppDataDir() -> call");

#ifdef _WIN32
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

    std::filesystem::create_directories(result);

    LOG_INFO("GetAppDataDir() -> return -> " + result.string());

    return result;
#else
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

    std::filesystem::create_directories(result);

    LOG_INFO("GetAppDataDir() -> return -> " + result.string());

    return result;
#endif
}

COLORREF GetPriceColor(double priceEx, AppConfig& config)
{
    if (priceEx > config.priceColorVeryHigh)
        return RGB(255, 60, 60); // red

    if (priceEx > config.priceColorHigh)
        return RGB(255, 220, 80); // yellow

    if (priceEx > config.priceColorMedium)
        return RGB(80, 255, 80); // green

    return RGB(160, 160, 160); // gray
}
