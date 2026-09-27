#pragma once

#include <optional>
#include <stop_token>
#include <string_view>

#include "core/ReleaseInfo.h"

class ReleaseProvider
{
public:
    virtual ~ReleaseProvider() = default;
    virtual std::optional<ReleaseInfo> LatestRelease(std::string_view assetName, const std::stop_token& stop) = 0;
};

class GitHubReleaseProvider final : public ReleaseProvider
{
public:
    std::optional<ReleaseInfo> LatestRelease(std::string_view assetName, const std::stop_token& stop) override;
};
