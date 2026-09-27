#include "UpdateChecker.h"

#include "common/ExceptionLogging.h"
#include "common/Logger.h"
#include "core/SelfUpdate.h"
#include "platform/PlatformPaths.h"
#include "platform/PlatformShell.h"

#include <cstdlib>
#include <utility>

namespace
{
std::string CurrentVersion()
{
    if (const char* pretend = std::getenv("RUNEHELPER_PRETEND_VERSION"); pretend && *pretend)
        return pretend;

    return RUNEHELPER_VERSION;
}
}

UpdateChecker::UpdateChecker(std::unique_ptr<ReleaseProvider> releaseProvider, std::unique_ptr<UpdateInstaller> installer)
    : releaseProvider_(releaseProvider ? std::move(releaseProvider) : std::make_unique<GitHubReleaseProvider>()),
      installer_(installer ? std::move(installer) : CreateUpdateInstaller())
{
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
                install_ = stop.stop_requested() ? UpdateInstall::Idle : UpdateInstall::Failed;
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

    std::optional<ReleaseInfo> fetched = releaseProvider_->LatestRelease(ReleaseAssetName(RUNEHELPER_BUILD_VARIANT), stop);

    if (!fetched || stop.stop_requested())
        return;

    ReleaseInfo release = std::move(*fetched);
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
    {
        std::lock_guard lock(mutex_);

        if (!release_.asset)
        {
            install_ = UpdateInstall::Failed;
            return;
        }

        asset = *release_.asset;
    }

    LOG_INFO("Update: downloading " + asset.url);

    const std::filesystem::path executable = ExecutablePath();
    const bool installed = installer_->Install(asset, executable, install_, installPercent_, stop);
    install_ = installed ? UpdateInstall::Installed : stop.stop_requested() ? UpdateInstall::Idle : UpdateInstall::Failed;
}
