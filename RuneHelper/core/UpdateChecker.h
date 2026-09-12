#pragma once

#include <atomic>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

class UpdateChecker
{
public:
    void Start();
    void Stop();

    bool IsChecking() const;
    bool HasUpdate() const;

    std::string LatestVersion() const;
    std::string DownloadUrl() const;

private:
    void Check(const std::stop_token& stop);

    std::jthread thread_;

    std::atomic<bool> checking_ = false;
    std::atomic<bool> hasUpdate_ = false;

    mutable std::mutex mutex_;
    std::string latestVersion_;
    std::string downloadUrl_;
};
