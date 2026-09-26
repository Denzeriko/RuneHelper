#include "UpdateChecker.h"

#include "core/ExceptionLogging.h"
#include "core/Logger.h"
#include "core/SelfUpdate.h"
#include "core/UpdateInstaller.h"
#include "platform/PlatformPaths.h"
#include "platform/PlatformShell.h"

#include <cpr/cpr.h>
#include "nlohmann/json.hpp"

#include <cstdint>
#include <cstdlib>

using json = nlohmann::json;

namespace
{
std::string CurrentVersion()
{
    if (const char* pretend = std::getenv("RUNEHELPER_PRETEND_VERSION"); pretend && *pretend)
        return pretend;

    return RUNEHELPER_VERSION;
}
}

void UpdateChecker::Start()
{
    checking_ = true;

    thread_ = std::jthread(
        [this](const std::stop_token& stop)
        {
            RunLoggingExceptions("UpdateChecker thread", [&] { Check(stop); });
            checking_ = false;
        }
    );
}

void UpdateChecker::Stop()
{
    if (thread_.joinable())
        thread_.request_stop();

    if (installThread_.joinable())
        installThread_.request_stop();
}

bool UpdateChecker::IsChecking() const
{
    return checking_;
}

bool UpdateChecker::HasUpdate() const
{
    return hasUpdate_;
}

bool UpdateChecker::CanInstall() const
{
    std::lock_guard lock(mutex_);
    return hasUpdate_ && release_.asset && !executable_.empty();
}

std::string UpdateChecker::LatestVersion() const
{
    std::lock_guard lock(mutex_);
    return release_.version;
}

std::string UpdateChecker::DownloadUrl() const
{
    std::lock_guard lock(mutex_);
    return release_.pageUrl;
}

void UpdateChecker::StartInstall()
{
    if (!CanInstall())
        return;

    UpdateInstall idle = UpdateInstall::Idle;

    if (!install_.compare_exchange_strong(idle, UpdateInstall::Downloading))
        return;

    installThread_ = std::jthread(
        [this](const std::stop_token& stop)
        {
            if (!RunLoggingExceptions("Update install thread", [&] { RunInstall(stop); }))
                install_ = UpdateInstall::Failed;
        }
    );
}

UpdateInstall UpdateChecker::Install() const
{
    return install_;
}

int UpdateChecker::InstallPercent() const
{
    return installPercent_;
}

std::filesystem::path UpdateChecker::ExecutablePath() const
{
    std::lock_guard lock(mutex_);
    return executable_;
}

void UpdateChecker::Check(const std::stop_token& stop)
{
    auto r = cpr::Get(
        cpr::Url{ "https://api.github.com/repos/Denzeriko/RuneHelper/releases/latest" },
        cpr::Header{ { "User-Agent", "RuneHelper/" RUNEHELPER_VERSION }, { "Accept", "application/vnd.github+json" } },
        cpr::Timeout{ 10000 },
        cpr::ProgressCallback{ [&stop](auto, auto, auto, auto, std::intptr_t) { return !stop.stop_requested(); } }
    );

    checking_ = false;

    std::filesystem::path executable = CurrentExecutablePath();

    if (!executable.empty())
    {
        RemoveUpdateLeftovers(UpdatePathsFor(executable));

        if (!CanReplace(UpdatePathsFor(executable)))
        {
            LOG_INFO("Update: " + PathToUtf8(executable.parent_path()) + " is not writable, Update opens the release page");
            executable.clear();
        }
    }

    if (stop.stop_requested())
        return;

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

    ReleaseInfo release = ParseRelease(j, ReleaseAssetName(RUNEHELPER_BUILD_VARIANT));
    const std::string current = CurrentVersion();

    LOG_INFO("Current version: " + current + (current != RUNEHELPER_VERSION ? " (pretended)" : ""));

    LOG_INFO("Latest version: " + release.version);

    const bool hasUpdate = !release.version.empty() && IsNewerVersion(release.version, current);

    if (hasUpdate && !release.asset)
        LOG_INFO("The release has no verifiable " + ReleaseAssetName(RUNEHELPER_BUILD_VARIANT) + ", Update opens the release page");

    {
        std::lock_guard lock(mutex_);
        release_ = std::move(release);
        executable_ = executable;
    }

    hasUpdate_ = hasUpdate;

    if (hasUpdate_)
        LOG_INFO("New version available: " + LatestVersion());
    else
        LOG_INFO("Application is up to date");
}

void UpdateChecker::RunInstall(const std::stop_token& stop)
{
    ReleaseAsset asset;
    UpdatePaths paths;

    {
        std::lock_guard lock(mutex_);

        if (!release_.asset)
        {
            install_ = UpdateInstall::Failed;
            return;
        }

        asset = *release_.asset;
        paths = UpdatePathsFor(executable_);
    }

    LOG_INFO("Update: downloading " + asset.url);

    if (!DownloadUpdate(asset, paths, installPercent_, stop))
    {
        install_ = stop.stop_requested() ? UpdateInstall::Idle : UpdateInstall::Failed;
        return;
    }

    install_ = UpdateInstall::Installing;
    install_ = ApplyUpdate(asset, paths) ? UpdateInstall::Installed : UpdateInstall::Failed;
}
