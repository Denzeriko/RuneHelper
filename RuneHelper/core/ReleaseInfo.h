#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

inline constexpr std::string_view kReleasesUrl = "https://github.com/Denzeriko/RuneHelper/releases/";

struct ReleaseAsset
{
    std::string url;
    std::uintmax_t size = 0;
    std::string sha256;
};

struct ReleaseInfo
{
    std::string version;
    std::string pageUrl;
    std::optional<ReleaseAsset> asset;
};

std::string ReleaseAssetName(std::string_view buildVariant);
ReleaseInfo ParseRelease(const nlohmann::json& release, std::string_view assetName);
bool IsNewerVersion(const std::string& latest, const std::string& current);
