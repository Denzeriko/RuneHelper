#include "Logger.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "platform/PlatformPaths.h"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>

namespace
{
constexpr uintmax_t kMaxLogSize = 16ULL * 1024 * 1024;
constexpr std::size_t kMaxPendingLines = 64;
constexpr int kRepeatBurst = 3;
constexpr std::chrono::seconds kRepeatWindow{ 60 };
constexpr std::size_t kMaxTrackedMessages = 1024;
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
    const std::filesystem::path& dir = GetUserDataDir();

    std::filesystem::create_directories(dir);

    const auto logPath = dir / "runehelper.log";
    const auto oldLogPath = dir / "runehelper.old.log";

    if (std::filesystem::exists(logPath) && std::filesystem::file_size(logPath) > kMaxLogSize)
    {
        std::error_code ec;
        std::filesystem::remove(oldLogPath, ec);
        std::filesystem::rename(logPath, oldLogPath, ec);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        file_.open(logPath, std::ios::app);

        if (!file_)
            return false;

        for (const std::string& line : pending_)
            file_ << line;

        pending_.clear();
        file_.flush();
    }

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
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    if (repeats_.size() > kMaxTrackedMessages)
        repeats_.clear();

    Repeat& repeat = repeats_[msg];
    std::string text = msg;

    if (repeat.written >= kRepeatBurst)
    {
        if (now - repeat.lastWritten < kRepeatWindow)
        {
            ++repeat.suppressed;
            return;
        }

        if (repeat.suppressed > 0)
            text += " (" + std::to_string(repeat.suppressed) + " repeats suppressed)";

        repeat.suppressed = 0;
    }

    ++repeat.written;
    repeat.lastWritten = now;

    const std::string line = TimeNow() + " [" + level + "] " + text + "\n";

#ifdef _DEBUG
    OutputDebugStringA(line.c_str());
#endif

    if (!file_)
    {
        if (pending_.size() < kMaxPendingLines)
            pending_.push_back(line);

        return;
    }

    file_ << line;
    file_.flush(); // make sure log will be saved after crash
}

std::string Logger::TimeNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buffer[32];
    const std::size_t length = std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);

    return std::string(buffer, length);
}
