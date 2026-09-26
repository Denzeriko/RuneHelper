#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include "core/ReleaseInfo.h"

enum class UpdateInstall
{
    Idle,
    Downloading,
    Installing,
    Installed,
    Failed
};

class UpdateChecker
{
public:
    void Start();
    void Stop();

    bool IsChecking() const;
    bool HasUpdate() const;
    bool CanInstall() const;

    std::string LatestVersion() const;
    std::string DownloadUrl() const;

    void StartInstall();
    UpdateInstall Install() const;
    int InstallPercent() const;
    std::filesystem::path ExecutablePath() const;

private:
    void Check(const std::stop_token& stop);
    void RunInstall(const std::stop_token& stop);

    std::atomic<bool> checking_ = false;
    std::atomic<bool> hasUpdate_ = false;
    std::atomic<UpdateInstall> install_ = UpdateInstall::Idle;
    std::atomic<int> installPercent_ = 0;

    mutable std::mutex mutex_;
    ReleaseInfo release_;
    std::filesystem::path executable_;

    std::jthread thread_;
    std::jthread installThread_;
};
