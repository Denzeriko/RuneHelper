#include "Logger.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <iostream>

Logger& Logger::Instance()
{
    static Logger logger;
    return logger;
}

bool Logger::Init()
{
    PWSTR appData = nullptr;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        return false;

    std::filesystem::path dir(appData);
    CoTaskMemFree(appData);

    dir /= "Denz";
    dir /= "RuneHelper";

    std::filesystem::create_directories(dir);

    auto logPath = dir / "runehelper.log";

    file_.open(logPath, std::ios::app);

    if (!file_)
        return false;

    Info("Logger initialized");
    return true;
}

void Logger::Info(const std::string& msg)
{
    Write("INFO", msg);
}

void Logger::Error(const std::string& msg)
{
    Write("ERROR", msg);
}

void Logger::Write(const char* level, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << msg << std::endl;

    if (!file_)
        return;

    file_
        << TimeNow()
        << " [" << level << "] "
        << msg
        << std::endl;
}

std::string Logger::TimeNow()
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_s(&tm, &t);

    std::ostringstream ss;

    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    return ss.str();
}