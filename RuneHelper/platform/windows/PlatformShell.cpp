#include "platform/PlatformShell.h"

#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

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

std::string ToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);

    if (size <= 0)
        return {};

    std::string text(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, text.data(), size, nullptr, nullptr);

    return text;
}

constexpr const wchar_t* kVersionKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
constexpr unsigned long kFirstWindows11Build = 22000;

std::wstring RegistryText(const wchar_t* name)
{
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, kVersionKey, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return {};

    return buffer;
}

DWORD RegistryNumber(const wchar_t* name)
{
    DWORD value = 0;
    DWORD size = sizeof(value);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, kVersionKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return 0;

    return value;
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

std::string DescribeSystem()
{
    std::wstring product = RegistryText(L"ProductName");
    const std::wstring build = RegistryText(L"CurrentBuild");
    const std::wstring release = RegistryText(L"DisplayVersion");

    if (std::wcstoul(build.c_str(), nullptr, 10) >= kFirstWindows11Build && product.starts_with(L"Windows 10"))
        product.replace(0, 10, L"Windows 11");

    std::string text = "System: " + (product.empty() ? std::string("Windows") : ToUtf8(product));

    if (!release.empty())
        text += " " + ToUtf8(release);

    if (!build.empty())
        text += ", build " + ToUtf8(build) + "." + std::to_string(RegistryNumber(L"UBR"));

    return text + "\n";
}
