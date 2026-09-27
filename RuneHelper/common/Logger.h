#pragma once

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Logger
{
public:
    static Logger& Instance();

    bool Init();

    void Info(const std::string& msg);
    void Error(const std::string& msg);

private:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Repeat
    {
        int written = 0;
        int suppressed = 0;
        std::chrono::steady_clock::time_point lastWritten{};
    };

    void Write(const char* level, const std::string& msg);
    static std::string TimeNow();

private:
    std::ofstream file_;
    std::mutex mutex_;
    std::vector<std::string> pending_;
    std::unordered_map<std::string, Repeat> repeats_;
};

#define LOG_INFO(msg) Logger::Instance().Info(msg)
#define LOG_ERROR(msg) Logger::Instance().Error(msg)
