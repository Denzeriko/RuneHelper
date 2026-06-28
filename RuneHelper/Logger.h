#pragma once

#include <fstream>
#include <mutex>
#include <string>

class Logger
{
public:
    static Logger& Instance();

    bool Init();
    void Info(const std::string& msg);
    void Error(const std::string& msg);

private:
    Logger() = default;

    std::mutex mutex_;
    std::ofstream file_;

    std::string TimeNow();
    void Write(const char* level, const std::string& msg);
};

#define LOG_INFO(msg)  Logger::Instance().Info(msg)
#define LOG_ERROR(msg) Logger::Instance().Error(msg)