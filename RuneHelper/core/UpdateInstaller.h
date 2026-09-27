#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <stop_token>

#include "core/ReleaseInfo.h"
#include "core/SelfUpdate.h"

enum class UpdateInstall
{
    Idle,
    Downloading,
    Installing,
    Installed,
    Failed
};

bool DownloadUpdate(const ReleaseAsset& asset, const UpdatePaths& paths, std::atomic<int>& percent, const std::stop_token& stop);
bool ApplyUpdate(const ReleaseAsset& asset, const UpdatePaths& paths);

class UpdateInstaller
{
public:
    virtual ~UpdateInstaller() = default;
    virtual bool Install(
        const ReleaseAsset& asset,
        const std::filesystem::path& executable,
        std::atomic<UpdateInstall>& status,
        std::atomic<int>& percent,
        const std::stop_token& stop
    ) = 0;
};

class BinaryUpdateInstaller final : public UpdateInstaller
{
public:
    bool Install(
        const ReleaseAsset& asset,
        const std::filesystem::path& executable,
        std::atomic<UpdateInstall>& status,
        std::atomic<int>& percent,
        const std::stop_token& stop
    ) override;
};

std::unique_ptr<UpdateInstaller> CreateUpdateInstaller();
