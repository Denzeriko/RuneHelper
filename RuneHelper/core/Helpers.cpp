#include "Helpers.h"
#ifdef _WIN32
#include <windows.h>
#endif
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

std::string VkToString(int vk)
{
    if (vk == 0)
        return "None";

#ifdef _WIN32
    UINT scan = MapVirtualKey(vk, MAPVK_VK_TO_VSC);

    char name[128]{};

    if (GetKeyNameTextA(scan << 16, name,sizeof(name)))
        return name;
#endif

    return std::to_string(vk);
}
