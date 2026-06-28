#pragma once

#include <atomic>
#include <string>
#include <thread>

class UpdateChecker
{
public:
    void Start();
    void Stop();

    bool IsChecking() const;
    bool HasUpdate() const;

    const std::string& LatestVersion() const;
    const std::string& DownloadUrl() const;

private:
    void Check();

    std::jthread thread_;

    std::atomic<bool> checking_ = false;
    std::atomic<bool> hasUpdate_ = false;

    std::string latestVersion_;
    std::string downloadUrl_;
};