#include "recipes/RecipeUpdater.h"

#include <cpr/cpr.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "nlohmann/json.hpp"

#include "core/AtomicFile.h"
#include "core/Logger.h"
#include "core/ThreadGuard.h"
#include "platform/PlatformPaths.h"
#include "recipes/RecipeDatabase.h"

using json = nlohmann::json;

namespace
{
const std::string& RecipeApiUrl()
{
    static const std::string url = []
    {
        if (const char* override = std::getenv("RUNEHELPER_RECIPE_API"); override && *override)
            return std::string(override);

        return std::string("https://denz.pw/poe2/combinations.json");
    }();

    return url;
}

const std::string& UserAgent()
{
    static const std::string agent =
        std::string("RuneHelper/") + RUNEHELPER_VERSION + " (+https://github.com/Denzeriko/RuneHelper)";

    return agent;
}
}

void RecipeUpdater::Start()
{
    thread_ = std::jthread(
        [this](const std::stop_token& stop)
        {
            RunLoggingExceptions("RecipeUpdater thread", [&] { Fetch(stop); });
        });
}

void RecipeUpdater::Stop()
{
    if (thread_.joinable())
        thread_.request_stop();
}

void RecipeUpdater::Fetch(const std::stop_token& stop)
{
    LOG_INFO("RecipeUpdater::Fetch() -> " + RecipeApiUrl());

    auto response = cpr::Get(
        cpr::Url{ RecipeApiUrl() },
        cpr::Header{
            { "User-Agent", UserAgent() },
            { "Accept", "application/json" }
        },
        cpr::Timeout{ 15000 },
        cpr::ProgressCallback{
            [&stop](auto, auto, auto, auto, std::intptr_t)
            {
                return !stop.stop_requested();
            }
        }
    );

    if (stop.stop_requested())
        return;

    if (response.error.code != cpr::ErrorCode::OK)
    {
        LOG_ERROR("RecipeUpdater CPR error: " + response.error.message);
        return;
    }

    if (response.status_code != 200)
    {
        LOG_ERROR("RecipeUpdater HTTP error: " + std::to_string(response.status_code));
        return;
    }

    json parsed = json::parse(response.text, nullptr, false);

    if (parsed.is_discarded() || !parsed.contains("combinations") || !parsed["combinations"].is_array() ||
        parsed["combinations"].empty())
    {
        LOG_ERROR("RecipeUpdater: downloaded combinations.json is not usable");
        return;
    }

    const std::filesystem::path path = DownloadedRecipeDatabasePath();

    if (!WriteFileAtomic(path, response.text))
    {
        LOG_ERROR("RecipeUpdater: could not write " + path.string());
        return;
    }

    LOG_INFO(
        "RecipeUpdater: stored " + std::to_string(parsed["combinations"].size()) +
        " combinations generated " + parsed.value("generated", std::string("?")) +
        ", it will be used on the next start"
    );
}
