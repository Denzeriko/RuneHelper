#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include "core/ReleaseInfo.h"
#include "core/ReleaseProvider.h"
#include "core/UpdateInstaller.h"

class UpdateChecker
{
public:
    UpdateChecker(std::unique_ptr<ReleaseProvider> releaseProvider = {}, std::unique_ptr<UpdateInstaller> installer = {});

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
    std::unique_ptr<ReleaseProvider> releaseProvider_;
    std::unique_ptr<UpdateInstaller> installer_;

    std::jthread thread_;
    std::jthread installThread_;
};
