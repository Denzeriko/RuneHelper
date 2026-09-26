#include "core/ReleaseInfo.h"

#include <algorithm>
#include <sstream>
#include <vector>

#include "core/JsonRead.h"
#include "core/Text.h"

namespace
{
constexpr std::string_view kDigestPrefix = "sha256:";
constexpr std::size_t kSha256HexLength = 64;
constexpr std::int64_t kMaxAssetSize = 256LL * 1024 * 1024;

bool IsLowerHex(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::optional<ReleaseAsset> ParseAsset(const nlohmann::json& asset)
{
    const std::string url = JsonValue(asset, "browser_download_url", "");
    const std::string digest = JsonValue(asset, "digest", "");
    const std::int64_t size = JsonValue<std::int64_t>(asset, "size", 0);

    if (!url.starts_with(std::string(kReleasesUrl) + "download/") || size <= 0 || size > kMaxAssetSize)
        return std::nullopt;

    if (!digest.starts_with(kDigestPrefix))
        return std::nullopt;

    const std::string sha256 = ToLowerAscii(std::string_view(digest).substr(kDigestPrefix.size()));

    if (sha256.size() != kSha256HexLength || !IsLowerHex(sha256))
        return std::nullopt;

    return ReleaseAsset{ url, static_cast<std::uintmax_t>(size), sha256 };
}

std::vector<int> ParseVersion(std::string version)
{
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V'))
        version.erase(version.begin());

    std::vector<int> parts;
    std::stringstream stream(version);
    std::string part;

    while (std::getline(stream, part, '.'))
    {
        try
        {
            parts.push_back(std::stoi(part));
        }
        catch (...)
        {
            parts.push_back(0);
        }
    }

    return parts;
}
}

std::string ReleaseAssetName(std::string_view buildVariant)
{
    constexpr std::string_view kLinuxPrefix = "linux-";

    if (buildVariant == "windows")
        return "RuneHelper-windows-x86_64.exe";

    if (buildVariant.starts_with(kLinuxPrefix) && buildVariant.size() > kLinuxPrefix.size())
        return "RuneHelper-linux-x86_64-" + std::string(buildVariant.substr(kLinuxPrefix.size()));

    return {};
}

ReleaseInfo ParseRelease(const nlohmann::json& release, std::string_view assetName)
{
    ReleaseInfo info;
    info.version = JsonValue(release, "tag_name", "");
    info.pageUrl = JsonValue(release, "html_url", "");

    if (!info.pageUrl.starts_with(kReleasesUrl))
        info.pageUrl = std::string(kReleasesUrl) + "latest";

    const auto assets = release.find("assets");

    if (assetName.empty() || assets == release.end() || !assets->is_array())
        return info;

    for (const nlohmann::json& asset : *assets)
    {
        if (asset.is_object() && JsonValue(asset, "name", "") == assetName)
        {
            info.asset = ParseAsset(asset);
            break;
        }
    }

    return info;
}

bool IsNewerVersion(const std::string& latest, const std::string& current)
{
    std::vector<int> l = ParseVersion(latest);
    std::vector<int> c = ParseVersion(current);

    const std::size_t n = std::max(l.size(), c.size());

    l.resize(n);
    c.resize(n);

    for (std::size_t i = 0; i < n; ++i)
    {
        if (l[i] != c[i])
            return l[i] > c[i];
    }

    return false;
}
