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

bool Spawn(const std::filesystem::path& program, const std::vector<std::string>& args, PROCESS_INFORMATION& process)
{
    std::wstring commandLine = L"\"" + program.wstring() + L"\"";

    for (const std::string& arg : args)
        commandLine += L" " + ToWide(arg);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    process = {};

    if (CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        return true;

    LOG_ERROR("CreateProcessW failed for " + ToUtf8(program.wstring()) + ", error " + std::to_string(GetLastError()));
    return false;
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

std::filesystem::path CurrentExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');

    while (true)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length == 0)
            return {};

        if (length < buffer.size())
        {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }

        buffer.resize(buffer.size() * 2);
    }
}

ProcessResult RunProcess(const std::filesystem::path& program, const std::vector<std::string>& args, std::chrono::milliseconds timeout)
{
    constexpr DWORD kTerminateWaitMs = 5000;

    ProcessResult result;
    PROCESS_INFORMATION process{};

    if (!Spawn(program, args, process))
        return result;

    result.started = true;
    CloseHandle(process.hThread);

    if (WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout.count())) == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, kTerminateWaitMs);
        result.timedOut = true;
    }
    else
    {
        DWORD code = 0;

        if (GetExitCodeProcess(process.hProcess, &code))
            result.exitCode = static_cast<int>(code);
    }

    CloseHandle(process.hProcess);
    return result;
}

bool StartProcess(const std::filesystem::path& program)
{
    PROCESS_INFORMATION process{};

    if (!Spawn(program, {}, process))
        return false;

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
