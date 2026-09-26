#pragma once

#include <atomic>
#include <stop_token>

#include "core/ReleaseInfo.h"
#include "core/SelfUpdate.h"

bool DownloadUpdate(const ReleaseAsset& asset, const UpdatePaths& paths, std::atomic<int>& percent, const std::stop_token& stop);
bool ApplyUpdate(const ReleaseAsset& asset, const UpdatePaths& paths);
