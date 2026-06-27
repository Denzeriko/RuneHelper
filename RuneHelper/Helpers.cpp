#include "Helpers.h"
#include <windows.h>
#include <regex>
#include <shlobj.h>

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

    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);

    if (size <= 0)
        return L"";

    std::wstring result(size - 1, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);

    return result;
}


std::filesystem::path GetAppDataDir() 
{
    PWSTR path = nullptr;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
        return ".";

    std::filesystem::path result(path);
    CoTaskMemFree(path);

    result /= "Denz";
    result /= "RuneHelper";

    std::filesystem::create_directories(result);

    return result;
}