#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

struct ProcessResult
{
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
};

bool OpenExternalUrl(const std::string& url);
std::string DescribeSystem();
std::filesystem::path CurrentExecutablePath();
ProcessResult RunProcess(
    const std::filesystem::path& program,
    const std::vector<std::string>& args,
    std::chrono::milliseconds timeout
);
bool StartProcess(const std::filesystem::path& program);
