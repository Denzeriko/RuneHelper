#include "UpdateChecker.h"

#include "Version.h"
#include "Logger.h"

#include <cpr/cpr.h>
#include "nlohmann/json.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

using json = nlohmann::json;

static std::string NormalizeVersion(std::string v)
{
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V'))
        v.erase(v.begin());

    return v;
}

static std::vector<int> ParseVersion(const std::string& version)
{
    std::vector<int> parts;

    std::stringstream ss(NormalizeVersion(version));

    std::string part;

    while (std::getline(ss, part, '.'))
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

static bool IsNewerVersion(const std::string& latest, const std::string& current)
{
    auto l = ParseVersion(latest);
    auto c = ParseVersion(current);

    size_t n = (std::max)(l.size(), c.size()); //#define NOMINMAX

    l.resize(n);
    c.resize(n);

    for (size_t i = 0; i < n; ++i)
    {
        if (l[i] > c[i])
            return true;

        if (l[i] < c[i])
            return false;
    }

    return false;
}

void UpdateChecker::Start()
{
    checking_ = true;

    thread_ = std::jthread([this]
        {
            Check();
        });
}

void UpdateChecker::Stop()
{
    if (thread_.joinable())
        thread_.request_stop();
}

bool UpdateChecker::IsChecking() const
{
    return checking_;
}

bool UpdateChecker::HasUpdate() const
{
    return hasUpdate_;
}

const std::string& UpdateChecker::LatestVersion() const
{
    return latestVersion_;
}

const std::string& UpdateChecker::DownloadUrl() const
{
    return downloadUrl_;
}

void UpdateChecker::Check()
{
    LOG_INFO("UpdateChecker::Check() -> call");

    auto r = cpr::Get(
        cpr::Url{
            "https://api.github.com/repos/Denzeriko/RuneHelper/releases/latest"
        },
        cpr::Header{
            { "User-Agent", "RuneHelper/" RUNEHELPER_VERSION },
            { "Accept", "application/vnd.github+json" }
        },
        cpr::Timeout{ 10000 }
    );

    checking_ = false;

    if (r.error.code != cpr::ErrorCode::OK)
    {
        LOG_ERROR("UpdateChecker CPR error: " + r.error.message);
        return;
    }

    if (r.status_code != 200)
    {
        LOG_ERROR("UpdateChecker HTTP error: " + std::to_string(r.status_code));
        return;
    }

    json j = json::parse(r.text, nullptr, false);

    if (j.is_discarded())
    {
        LOG_ERROR("UpdateChecker JSON parse failed");

        return;
    }

    latestVersion_ =j.value("tag_name", "");

    downloadUrl_ = j.value("html_url", "");

    LOG_INFO("Current version: " + std::string(RUNEHELPER_VERSION));

    LOG_INFO("Latest version: " + latestVersion_);

    if (!latestVersion_.empty())
        hasUpdate_ = IsNewerVersion(latestVersion_, RUNEHELPER_VERSION);

    if (hasUpdate_)
        LOG_INFO("New version available: " + latestVersion_);
    else
        LOG_INFO("Application is up to date");
}
