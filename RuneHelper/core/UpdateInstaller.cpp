#include "core/UpdateInstaller.h"

#include <cpr/cpr.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>

#include "core/Logger.h"
#include "platform/PlatformPaths.h"
#include "platform/PlatformShell.h"

namespace
{
constexpr std::chrono::seconds kSelfCheckTimeout{ 10 };
constexpr std::int32_t kConnectTimeoutMs = 10000;
constexpr std::chrono::seconds kStallTimeout{ 30 };

void Discard(const std::filesystem::path& file)
{
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

bool PassesSelfCheck(const std::filesystem::path& binary)
{
    const ProcessResult result = RunProcess(binary, { "--version" }, kSelfCheckTimeout);

    if (!result.started)
    {
        LOG_ERROR("Update: the new binary could not be started");
        return false;
    }

    if (result.timedOut)
    {
        LOG_INFO("Update: the new binary kept running after --version, taken as a version without that switch");
        return true;
    }

    if (result.exitCode != 0)
    {
        LOG_ERROR("Update: the new binary exited with code " + std::to_string(result.exitCode) + " on --version");
        return false;
    }

    return true;
}
}

bool DownloadUpdate(const ReleaseAsset& asset, const UpdatePaths& paths, std::atomic<int>& percent, const std::stop_token& stop)
{
    percent = 0;

    cpr::Response response;
    std::int64_t received = 0;
    auto lastProgress = std::chrono::steady_clock::now();

    {
        std::ofstream file(paths.download, std::ios::binary | std::ios::trunc);

        if (!file)
        {
            LOG_ERROR("Update: cannot write " + PathToUtf8(paths.download));
            return false;
        }

        response = cpr::Download(
            file,
            cpr::Url{ asset.url },
            cpr::Header{ { "User-Agent", "RuneHelper/" RUNEHELPER_VERSION } },
            cpr::ConnectTimeout{ kConnectTimeoutMs },
            cpr::ProgressCallback{ [&](auto total, auto now, auto, auto, std::intptr_t)
                                   {
                                       const auto clock = std::chrono::steady_clock::now();

                                       if (static_cast<std::int64_t>(now) != received)
                                       {
                                           received = static_cast<std::int64_t>(now);
                                           lastProgress = clock;
                                       }

                                       if (total > 0)
                                           percent = static_cast<int>(now * 100 / total);

                                       return !stop.stop_requested() && clock - lastProgress < kStallTimeout;
                                   } }
        );
    }

    if (stop.stop_requested())
    {
        Discard(paths.download);
        return false;
    }

    if (response.error.code != cpr::ErrorCode::OK || response.status_code != 200)
    {
        LOG_ERROR("Update: download failed, HTTP " + std::to_string(response.status_code) + " " + response.error.message);
        Discard(paths.download);
        return false;
    }

    if (!FileMatches(paths.download, asset.size, asset.sha256))
    {
        LOG_ERROR("Update: the download does not match the size and SHA-256 GitHub published");
        Discard(paths.download);
        return false;
    }

    percent = 100;
    return true;
}

bool ApplyUpdate(const ReleaseAsset& asset, const UpdatePaths& paths)
{
    if (!MakeExecutable(paths.download) || !PassesSelfCheck(paths.download))
    {
        Discard(paths.download);
        return false;
    }

    if (!FileMatches(paths.download, asset.size, asset.sha256))
    {
        LOG_ERROR("Update: the new binary changed or disappeared after its first run, an antivirus may have taken it");
        Discard(paths.download);
        return false;
    }

    if (!SwapExecutable(paths))
    {
        Discard(paths.download);
        return false;
    }

    LOG_INFO("Update: installed, the previous binary is kept as " + PathToUtf8(paths.backup));
    return true;
}
