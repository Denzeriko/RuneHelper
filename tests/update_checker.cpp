#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "core/UpdateChecker.h"
#include "platform/PlatformShell.h"

namespace
{
using namespace std::chrono_literals;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Predicate>
void WaitUntil(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;

    while (!predicate())
    {
        Require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(1ms);
    }
}

class Gate
{
public:
    bool Enter(const std::stop_token& stop)
    {
        std::unique_lock lock(mutex_);
        entered_ = true;
        condition_.notify_all();
        return condition_.wait_for(lock, stop, 5s, [this] { return released_; });
    }

    void WaitForEntry()
    {
        std::unique_lock lock(mutex_);
        Require(condition_.wait_for(lock, 5s, [this] { return entered_; }), "worker did not enter the expected phase");
    }

    void Release()
    {
        std::lock_guard lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable_any condition_;
    bool entered_ = false;
    bool released_ = false;
};

ReleaseInfo NewRelease()
{
    return { "9999.0.0", "https://example.invalid/release", ReleaseAsset{ "https://example.invalid/binary", 123, "digest" } };
}

class FakeProvider final : public ReleaseProvider
{
public:
    std::optional<ReleaseInfo> LatestRelease(std::string_view assetName, const std::stop_token& stop) override
    {
        ++calls;
        Require(assetName == ReleaseAssetName(RUNEHELPER_BUILD_VARIANT), "wrong build variant requested");

        if (!pending.Enter(stop))
        {
            cancelled = stop.stop_requested();
            return releaseOnCancel ? release : std::nullopt;
        }

        if (fail)
            throw std::runtime_error("provider failure");

        return release;
    }

    Gate pending;
    std::optional<ReleaseInfo> release = NewRelease();
    bool fail = false;
    bool releaseOnCancel = false;
    std::atomic<int> calls = 0;
    std::atomic<bool> cancelled = false;
};

class FakeInstaller final : public UpdateInstaller
{
public:
    enum class Outcome
    {
        Success,
        Failure,
        Exception
    };

    bool Install(
        const ReleaseAsset& asset,
        const std::filesystem::path& executable,
        std::atomic<UpdateInstall>& status,
        std::atomic<int>& percent,
        const std::stop_token& stop
    ) override
    {
        ++calls;
        receivedAsset = asset;
        receivedExecutable = executable;
        percent = 37;

        if (!downloading.Enter(stop))
        {
            cancelled = stop.stop_requested();

            if (throwOnCancel)
                throw std::runtime_error("download cancelled");

            return false;
        }

        if (!Complete(downloadOutcome))
            return false;

        percent = 100;
        status = UpdateInstall::Installing;

        if (!installing.Enter(stop))
        {
            cancelled = stop.stop_requested();
            return committedOnCancel;
        }

        return Complete(applyOutcome);
    }

    Gate downloading;
    Gate installing;
    Outcome downloadOutcome = Outcome::Success;
    Outcome applyOutcome = Outcome::Success;
    bool throwOnCancel = false;
    bool committedOnCancel = false;
    ReleaseAsset receivedAsset;
    std::filesystem::path receivedExecutable;
    std::atomic<int> calls = 0;
    std::atomic<bool> cancelled = false;

private:
    static bool Complete(Outcome outcome)
    {
        if (outcome == Outcome::Exception)
            throw std::runtime_error("installer failure");

        return outcome == Outcome::Success;
    }
};

std::filesystem::path& TestExecutable()
{
    static std::filesystem::path path;
    return path;
}

class Fixture
{
public:
    Fixture() : Fixture(std::make_unique<FakeProvider>(), std::make_unique<FakeInstaller>()) {}

    ~Fixture() { checker.Stop(); }

    void CheckRelease()
    {
        checker.Start();
        provider.pending.WaitForEntry();
        Require(checker.IsChecking(), "checking must remain true while the provider is pending");
        provider.pending.Release();
        WaitUntil([this] { return !checker.IsChecking(); }, "release check did not finish");
    }

    void BeginInstall()
    {
        CheckRelease();
        Require(checker.CanInstall(), "new release should be installable");
        checker.StartInstall();
        installer.downloading.WaitForEntry();
        Require(checker.Install() == UpdateInstall::Downloading, "download state was not published");
        Require(checker.InstallPercent() == 37, "download progress was not published");
        Require(installer.receivedExecutable == TestExecutable(), "wrong executable passed to installer");
        const ReleaseAsset expected = *NewRelease().asset;
        Require(
            installer.receivedAsset.url == expected.url && installer.receivedAsset.size == expected.size &&
                installer.receivedAsset.sha256 == expected.sha256,
            "release asset metadata was not passed to installer"
        );
        checker.StartInstall();
        Require(installer.calls == 1, "duplicate install must not start a second operation");
    }

    void BeginApply()
    {
        installer.downloading.Release();
        installer.installing.WaitForEntry();
        Require(checker.Install() == UpdateInstall::Installing, "installation state was not published");
        Require(checker.InstallPercent() == 100, "completed download should report 100 percent");
    }

    FakeProvider& provider;
    FakeInstaller& installer;
    UpdateChecker checker;

private:
    Fixture(std::unique_ptr<FakeProvider> source, std::unique_ptr<FakeInstaller> destination)
        : provider(*source), installer(*destination), checker(std::move(source), std::move(destination))
    {
    }
};

void TestVersions()
{
    for (const std::string& version : { std::string(RUNEHELPER_VERSION), std::string("0.0.0"), std::string() })
    {
        Fixture fixture;
        fixture.provider.release->version = version;
        fixture.CheckRelease();
        Require(!fixture.checker.HasUpdate() && !fixture.checker.CanInstall(), "non-newer version offered an update");
        fixture.checker.StartInstall();
        Require(fixture.installer.calls == 0, "installer called without a newer release");
    }
}

void TestReleaseMetadata()
{
    Fixture fixture;
    fixture.CheckRelease();
    Require(fixture.checker.HasUpdate() && fixture.checker.CanInstall(), "new version was not published");
    Require(fixture.checker.LatestVersion() == NewRelease().version, "wrong release version");
    Require(fixture.checker.DownloadUrl() == NewRelease().pageUrl, "wrong release page");
}

void TestProviderFailures()
{
    for (bool throws : { false, true })
    {
        Fixture fixture;
        fixture.provider.release.reset();
        fixture.provider.fail = throws;
        fixture.CheckRelease();
        Require(!fixture.checker.HasUpdate() && !fixture.checker.CanInstall(), "failed check offered an update");
        fixture.checker.StartInstall();
        Require(fixture.installer.calls == 0, "installer called after a failed check");
    }
}

void TestMissingAsset()
{
    Fixture fixture;
    fixture.provider.release->asset.reset();
    fixture.CheckRelease();
    Require(fixture.checker.HasUpdate() && !fixture.checker.CanInstall(), "missing asset must disable automatic install");
    Require(fixture.checker.DownloadUrl() == NewRelease().pageUrl, "missing asset must preserve the release page");
    fixture.checker.StartInstall();
    Require(fixture.installer.calls == 0, "installer called without an asset");
}

void TestCheckCancellation()
{
    for (bool lateRelease : { false, true })
    {
        Fixture fixture;
        fixture.provider.releaseOnCancel = lateRelease;
        fixture.checker.Start();
        fixture.provider.pending.WaitForEntry();
        fixture.checker.Stop();
        WaitUntil([&] { return !fixture.checker.IsChecking(); }, "cancelled check did not finish");
        Require(fixture.provider.cancelled, "provider did not receive cancellation");
        Require(!fixture.checker.HasUpdate(), "cancelled check published a release");
    }
}

void TestInstallation()
{
    Fixture fixture;
    fixture.BeginInstall();
    fixture.BeginApply();
    fixture.installer.installing.Release();
    WaitUntil([&] { return fixture.checker.Install() == UpdateInstall::Installed; }, "successful install was not published");
    fixture.checker.StartInstall();
    Require(fixture.installer.calls == 1, "completed installation must not be started again");
}

void TestInstallFailures()
{
    for (bool duringDownload : { false, true })
    {
        for (const auto outcome : { FakeInstaller::Outcome::Failure, FakeInstaller::Outcome::Exception })
        {
            Fixture fixture;
            if (duringDownload)
                fixture.installer.downloadOutcome = outcome;
            else
                fixture.installer.applyOutcome = outcome;
            fixture.BeginInstall();

            if (duringDownload)
                fixture.installer.downloading.Release();
            else
            {
                fixture.BeginApply();
                fixture.installer.installing.Release();
            }

            WaitUntil([&] { return fixture.checker.Install() == UpdateInstall::Failed; }, "installer failure was not published");
            Require(fixture.checker.InstallPercent() == (duringDownload ? 37 : 100), "failure changed download progress");
        }
    }
}

void TestDownloadCancellation()
{
    for (bool throws : { false, true })
    {
        Fixture fixture;
        fixture.installer.throwOnCancel = throws;
        fixture.BeginInstall();
        fixture.checker.Stop();
        WaitUntil([&] { return fixture.checker.Install() != UpdateInstall::Downloading; }, "download did not stop");
        Require(fixture.installer.cancelled, "installer did not receive cancellation");
        Require(fixture.checker.Install() == UpdateInstall::Idle, "cancelled download must return to Idle");
    }
}

void TestApplyCancellation()
{
    for (bool committed : { false, true })
    {
        Fixture fixture;
        fixture.installer.committedOnCancel = committed;
        fixture.BeginInstall();
        fixture.BeginApply();
        fixture.checker.Stop();
        WaitUntil([&] { return fixture.checker.Install() != UpdateInstall::Installing; }, "installation did not finish");
        Require(fixture.installer.cancelled, "installer did not receive cancellation during apply");
        Require(
            fixture.checker.Install() == (committed ? UpdateInstall::Installed : UpdateInstall::Idle),
            "cancellation must preserve a committed update and discard an uncommitted one"
        );
    }
}

}

std::filesystem::path CurrentExecutablePath()
{
    return TestExecutable();
}

ProcessResult RunProcess(const std::filesystem::path&, const std::vector<std::string>&, std::chrono::milliseconds)
{
    throw std::runtime_error("test attempted to run a real update binary");
}

int main()
{
    unsetenv("RUNEHELPER_PRETEND_VERSION");
    std::string temporary = (std::filesystem::temp_directory_path() / "runehelper-update-tests-XXXXXX").string();

    if (!mkdtemp(temporary.data()))
        return 1;

    TestExecutable() = std::filesystem::path(temporary) / "RuneHelper";
    int failures = 0;
    const std::pair<const char*, void (*)()> cases[] = {
        { "versions", TestVersions },
        { "release metadata", TestReleaseMetadata },
        { "provider failures", TestProviderFailures },
        { "missing asset", TestMissingAsset },
        { "check cancellation", TestCheckCancellation },
        { "installation", TestInstallation },
        { "install failures", TestInstallFailures },
        { "download cancellation", TestDownloadCancellation },
        { "apply cancellation", TestApplyCancellation },
    };

    for (const auto& [name, run] : cases)
    {
        try
        {
            run();
            std::printf("PASS %s\n", name);
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::fprintf(stderr, "FAIL %s: %s\n", name, error.what());
        }
    }

    std::error_code error;
    std::filesystem::remove_all(temporary, error);
    return failures == 0 ? 0 : 1;
}
