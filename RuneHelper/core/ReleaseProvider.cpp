#include "core/ReleaseProvider.h"

#include <cstdint>
#include <string>

#include <cpr/cpr.h>
#include "nlohmann/json.hpp"

#include "common/Logger.h"

std::optional<ReleaseInfo> GitHubReleaseProvider::LatestRelease(std::string_view assetName, const std::stop_token& stop)
{
    auto response = cpr::Get(
        cpr::Url{ "https://api.github.com/repos/Denzeriko/RuneHelper/releases/latest" },
        cpr::Header{ { "User-Agent", "RuneHelper/" RUNEHELPER_VERSION }, { "Accept", "application/vnd.github+json" } },
        cpr::Timeout{ 10000 },
        cpr::ProgressCallback{ [&stop](auto, auto, auto, auto, std::intptr_t) { return !stop.stop_requested(); } }
    );

    if (stop.stop_requested())
        return std::nullopt;

    if (response.error.code != cpr::ErrorCode::OK)
    {
        LOG_ERROR("UpdateChecker CPR error: " + response.error.message);
        return std::nullopt;
    }

    if (response.status_code != 200)
    {
        LOG_ERROR("UpdateChecker HTTP error: " + std::to_string(response.status_code));
        return std::nullopt;
    }

    nlohmann::json value = nlohmann::json::parse(response.text, nullptr, false);

    if (value.is_discarded())
    {
        LOG_ERROR("UpdateChecker JSON parse failed");
        return std::nullopt;
    }

    return ParseRelease(value, assetName);
}
