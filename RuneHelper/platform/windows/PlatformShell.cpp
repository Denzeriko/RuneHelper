#include "platform/PlatformShell.h"

#include <windows.h>
#include <shellapi.h>

#include "core/Logger.h"

namespace
{
std::wstring ToWide(const std::string& text)
{
    if (text.empty())
        return L"";

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

    if (size <= 0)
        return L"";

    std::wstring wide(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);

    return wide;
}
}

bool OpenExternalUrl(const std::string& url)
{
    if (url.empty())
        return false;

    const std::wstring wide = ToWide(url);

    const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        LOG_ERROR("ShellExecuteW could not open " + url);
        return false;
    }

    return true;
}
