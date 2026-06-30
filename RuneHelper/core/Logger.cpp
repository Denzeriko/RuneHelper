#include "Logger.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace
{
    constexpr uintmax_t kMaxLogSize = 16 * 1024 * 1024; // 16 MB
}

Logger& Logger::Instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (file_.is_open())
        file_.close();
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

    const auto logPath = dir / "runehelper.log";
    const auto oldLogPath = dir / "runehelper.old.log";

    if (std::filesystem::exists(logPath) && std::filesystem::file_size(logPath) > kMaxLogSize)
    {
        std::error_code ec;
        std::filesystem::remove(oldLogPath, ec);
        std::filesystem::rename(logPath, oldLogPath, ec);
    }

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

void Logger::Debug(const std::string& msg)
{
#ifdef _DEBUG
    Write("DEBUG", msg);
#else
    (void)msg;
#endif
}

void Logger::Write(const char* level, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string line = TimeNow() + " [" + level + "] " + msg + "\n";

#ifdef _DEBUG
    OutputDebugStringA(line.c_str());
#endif

    if (!file_)
        return;

    file_ << line;
    file_.flush(); //make sure log will be saved after crash
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