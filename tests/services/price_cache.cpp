#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "platform/PlatformPaths.h"
#include "price/PriceCache.h"

namespace
{
using namespace std::chrono_literals;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Reply
{
    PriceTable table;
    bool fail = false;
};

struct ProviderControl
{
    std::mutex mutex;
    std::condition_variable_any condition;
    std::vector<std::string> leagues;
    std::deque<Reply> replies;
    bool cancelled = false;
    bool returnAfterCancel = false;

    void WaitForCalls(std::size_t count)
    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 5s, [&] { return leagues.size() >= count; }), "provider did not receive the request");
    }

    void Respond(PriceTable table = {}, bool fail = false)
    {
        std::lock_guard lock(mutex);
        replies.push_back({ std::move(table), fail });
        condition.notify_all();
    }
};

PriceTable FreshPrices()
{
    return { { { "Runic Alloy", { 30.0 } }, { "New Item", { 7.0 } } }, 200.0, true };
}

class FakeProvider final : public PriceProvider
{
public:
    explicit FakeProvider(std::shared_ptr<ProviderControl> control) : control_(std::move(control)) {}

    PriceTable DownloadPrices(const std::string& league, const std::stop_token& stop) override
    {
        std::unique_lock lock(control_->mutex);
        control_->leagues.push_back(league);
        control_->condition.notify_all();
        const bool ready = control_->condition.wait_for(lock, stop, 5s, [&] { return !control_->replies.empty(); });

        if (stop.stop_requested())
        {
            control_->cancelled = true;
            return control_->returnAfterCancel ? FreshPrices() : PriceTable{};
        }

        Require(ready, "provider response timed out");
        Reply reply = std::move(control_->replies.front());
        control_->replies.pop_front();

        if (reply.fail)
            throw std::runtime_error("download failed");

        return std::move(reply.table);
    }

private:
    std::shared_ptr<ProviderControl> control_;
};

enum class SaveFailure
{
    None,
    ReturnFalse,
    Exception
};

struct StoreControl
{
    std::mutex mutex;
    std::condition_variable condition;
    std::unordered_map<std::string, PriceDump> dumps;
    std::vector<std::string> savedLeagues;
    SaveFailure saveFailure = SaveFailure::None;
    bool loadFailure = false;
    bool blockLoad = false;
    bool loading = false;
    bool releaseLoad = false;
    bool blockSave = false;
    bool saving = false;
    bool releaseSave = false;

    std::optional<PriceDump> Read(const std::string& league)
    {
        std::lock_guard lock(mutex);
        const auto found = dumps.find(league);
        return found == dumps.end() ? std::nullopt : std::optional<PriceDump>(found->second);
    }

    void WaitForSave()
    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 5s, [&] { return saving; }), "store did not receive a save");
    }

    void WaitForLoad()
    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 5s, [&] { return loading; }), "store did not receive a load");
    }

    void ReleaseLoad()
    {
        std::lock_guard lock(mutex);
        releaseLoad = true;
        condition.notify_all();
    }

    void ReleaseSave()
    {
        std::lock_guard lock(mutex);
        releaseSave = true;
        condition.notify_all();
    }
};

class FakeStore final : public PriceStore
{
public:
    explicit FakeStore(std::shared_ptr<StoreControl> control) : control_(std::move(control)) {}

    std::optional<PriceDump> Load(const std::string& league) override
    {
        std::unique_lock lock(control_->mutex);
        if (control_->loadFailure)
            throw std::runtime_error("store read failed");

        const auto found = control_->dumps.find(league);
        const auto dump = found == control_->dumps.end() ? std::nullopt : std::optional<PriceDump>(found->second);
        control_->loading = true;
        control_->condition.notify_all();

        if (control_->blockLoad)
            Require(control_->condition.wait_for(lock, 5s, [&] { return control_->releaseLoad; }), "store read timed out");

        return dump;
    }

    bool Save(const std::string& league, const PriceDump& dump) override
    {
        std::unique_lock lock(control_->mutex);
        control_->saving = true;
        control_->savedLeagues.push_back(league);
        control_->condition.notify_all();

        if (control_->blockSave)
            Require(control_->condition.wait_for(lock, 5s, [&] { return control_->releaseSave; }), "store write timed out");

        if (control_->saveFailure == SaveFailure::Exception)
            throw std::runtime_error("store write failed");
        if (control_->saveFailure == SaveFailure::ReturnFalse)
            return false;

        control_->dumps[league] = dump;
        return true;
    }

private:
    std::shared_ptr<StoreControl> control_;
};

struct Fixture
{
    std::shared_ptr<ProviderControl> provider = std::make_shared<ProviderControl>();
    std::shared_ptr<StoreControl> store = std::make_shared<StoreControl>();
    std::unique_ptr<PriceCache> cache =
        std::make_unique<PriceCache>(std::make_unique<FakeProvider>(provider), std::make_unique<FakeStore>(store));

    Fixture()
    {
        store->dumps["A"] = { { { "Runic Alloy", { 10.0 } }, { "Old Item", { 5.0 } } }, 100.0, 1234 };
        store->dumps["B"] = { { { "Runic Alloy", { 99.0 } } }, 900.0, 1234 };
    }

    ~Fixture()
    {
        store->ReleaseLoad();
        store->ReleaseSave();
    }

    void Begin(const std::string& league = "A")
    {
        cache->SetLeague(league);
        cache->ForceRefreshAsync();
        provider->WaitForCalls(1);
    }

    void Wait()
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (cache->IsRefreshInProgress())
        {
            Require(std::chrono::steady_clock::now() < deadline, "refresh did not finish");
            std::this_thread::sleep_for(1ms);
        }
    }
};

void TestSuccessfulRefresh()
{
    Fixture fixture;
    fixture.Begin();
    const auto downloading = fixture.cache->Status();
    Require(
        downloading.downloading && downloading.priceCount == 2 && downloading.updatedAt == 1234 && !downloading.refreshFailed,
        "refresh status lost cached metadata"
    );
    fixture.cache->ForceRefreshAsync();
    fixture.provider->Respond(FreshPrices());
    fixture.Wait();
    Require(fixture.provider->leagues == std::vector<std::string>{ "A" }, "duplicate refresh started another request");
    Require(
        fixture.cache->GetPrice("Runic Alloy") == 30.0 && fixture.cache->GetPrice("New Item") == 7.0,
        "fresh prices were not published"
    );
    Require(!fixture.cache->GetPrice("Old Item"), "complete refresh kept an obsolete item");
    Require(fixture.cache->DivineRate() == 200.0, "fresh currency rate was not published");
    const auto saved = fixture.store->Read("A");
    Require(
        saved && saved->items.at("Runic Alloy").ex == 30.0 && saved->divineToEx == 200.0 && saved->updatedAt > 1234,
        "fresh prices or metadata were not saved"
    );
    const auto status = fixture.cache->Status();
    Require(
        !status.downloading && status.priceCount == 2 && status.updatedAt == saved->updatedAt && !status.refreshFailed,
        "successful refresh status is incorrect"
    );
    fixture.cache->RefreshIfNeeded();
    Require(!fixture.cache->IsRefreshInProgress(), "fresh complete cache was downloaded again");
}

void TestNetworkFailureAndRecovery()
{
    for (bool throws : { false, true })
    {
        Fixture fixture;
        fixture.Begin();
        const auto version = fixture.cache->Version();
        fixture.provider->Respond({}, throws);
        fixture.Wait();
        Require(
            fixture.cache->GetPrice("Runic Alloy") == 10.0 && fixture.cache->DivineRate() == 100.0,
            "failed download discarded working prices"
        );
        Require(fixture.cache->Version() == version && fixture.store->savedLeagues.empty(), "failed download published a dump");
        const auto failed = fixture.cache->Status();
        Require(
            !failed.downloading && failed.refreshFailed && failed.priceCount == 2 && failed.updatedAt == 1234,
            "failed refresh status lost cached metadata"
        );
        fixture.cache->RefreshIfNeeded();
        Require(!fixture.cache->IsRefreshInProgress(), "failed request was retried without backoff");
        fixture.cache->ForceRefreshAsync();
        fixture.provider->WaitForCalls(2);
        fixture.provider->Respond(FreshPrices());
        fixture.Wait();
        Require(fixture.cache->GetPrice("Runic Alloy") == 30.0, "forced refresh did not recover from failure");
        const auto recovered = fixture.cache->Status();
        Require(!recovered.refreshFailed && recovered.updatedAt > 1234, "recovered refresh kept a failed status");
    }
}

void TestPartialRefresh()
{
    Fixture fixture;
    fixture.Begin();
    fixture.provider->Respond({ { { "Runic Alloy", { 20.0 } } }, 0.0, false });
    fixture.Wait();
    Require(
        fixture.cache->GetPrice("Runic Alloy") == 20.0 && fixture.cache->GetPrice("Old Item") == 5.0,
        "partial refresh did not merge with cached prices"
    );
    Require(fixture.cache->DivineRate() == 100.0, "partial refresh discarded the known currency rate");
    const auto status = fixture.cache->Status();
    Require(status.refreshFailed && status.updatedAt == 1234, "partial refresh status claimed a complete update");
    const auto saved = fixture.store->Read("A");
    Require(
        saved && saved->items.at("Old Item").ex == 5.0 && saved->updatedAt == 1234,
        "partial refresh lost old prices or claimed a complete refresh timestamp"
    );
    fixture.cache->RefreshIfNeeded();
    Require(!fixture.cache->IsRefreshInProgress(), "partial refresh retry ignored backoff");
}

void TestStoreReadFailure()
{
    for (bool throws : { false, true })
    {
        Fixture fixture;
        fixture.store->loadFailure = throws;
        fixture.Begin("Missing");
        Require(fixture.cache->GetPriceCount() == 0, "missing dump supplied prices");
        fixture.provider->Respond(FreshPrices());
        fixture.Wait();
        Require(fixture.cache->GetPrice("Runic Alloy") == 30.0, "failed cache read prevented a network refresh");
    }
}

void TestStoreWriteFailure()
{
    for (const auto failure : { SaveFailure::ReturnFalse, SaveFailure::Exception })
    {
        Fixture fixture;
        fixture.store->saveFailure = failure;
        fixture.Begin();
        fixture.provider->Respond(FreshPrices());
        fixture.Wait();
        Require(
            fixture.cache->GetPrice("Runic Alloy") == 30.0 && fixture.cache->DivineRate() == 200.0,
            "failed save discarded downloaded prices"
        );
        Require(fixture.store->Read("A")->items.at("Runic Alloy").ex == 10.0, "failed save changed the stored prices");
        const auto status = fixture.cache->Status();
        Require(!status.refreshFailed && status.updatedAt > 1234, "failed save marked downloaded prices as stale");
        fixture.store->saveFailure = SaveFailure::None;
        fixture.cache->ForceRefreshAsync();
        fixture.provider->WaitForCalls(2);
        fixture.provider->Respond(FreshPrices());
        fixture.Wait();
        Require(fixture.store->Read("A")->items.at("Runic Alloy").ex == 30.0, "store did not recover after a failed save");
    }
}

void TestLeagueSwitch()
{
    for (bool returnToOriginal : { false, true })
    {
        Fixture fixture;
        fixture.Begin();
        fixture.cache->SetLeague("B");
        if (returnToOriginal)
            fixture.cache->SetLeague("A");
        const auto version = fixture.cache->Version();
        fixture.provider->Respond(FreshPrices());
        fixture.Wait();
        Require(
            fixture.cache->GetPrice("Runic Alloy") == (returnToOriginal ? 10.0 : 99.0),
            "request from before the league switch replaced current prices"
        );
        Require(fixture.cache->Version() == version && fixture.store->savedLeagues.empty(), "stale response was published or saved");
    }
}

void TestStaleFailure()
{
    for (bool throws : { false, true })
    {
        Fixture fixture;
        fixture.Begin();
        fixture.cache->SetLeague("Missing");
        fixture.provider->Respond({}, throws);
        fixture.Wait();
        const auto status = fixture.cache->Status();
        Require(
            !status.refreshFailed && status.priceCount == 0 && status.updatedAt == 0,
            "new league status retained metadata from a stale request"
        );
        fixture.cache->RefreshIfNeeded();
        Require(fixture.cache->IsRefreshInProgress(), "old league failure delayed the new league request");
        fixture.provider->WaitForCalls(2);
        fixture.provider->Respond(FreshPrices());
        fixture.Wait();
        Require(fixture.provider->leagues == std::vector<std::string>{ "A", "Missing" }, "refresh used the wrong league");
        Require(fixture.cache->GetPrice("Runic Alloy") == 30.0, "new league request did not publish prices");
    }
}

void TestSwitchDuringSave()
{
    Fixture fixture;
    fixture.store->blockSave = true;
    fixture.Begin();
    fixture.provider->Respond(FreshPrices());
    fixture.store->WaitForSave();
    fixture.cache->SetLeague("B");
    fixture.store->ReleaseSave();
    fixture.Wait();
    Require(
        fixture.cache->GetPrice("Runic Alloy") == 99.0 && fixture.cache->DivineRate() == 900.0,
        "pending save changed the active league prices"
    );
    Require(
        fixture.store->Read("A")->items.at("Runic Alloy").ex == 30.0 && fixture.store->Read("B")->items.at("Runic Alloy").ex == 99.0,
        "pending save wrote to the wrong league"
    );
}

void TestCancellation()
{
    for (bool lateResult : { false, true })
    {
        Fixture fixture;
        fixture.provider->returnAfterCancel = lateResult;
        fixture.Begin();
        fixture.cache.reset();
        Require(fixture.provider->cancelled, "cache destruction did not cancel the active download");
        Require(fixture.store->savedLeagues.empty(), "cancelled download saved a result");
    }
}

void TestLateStoreRead()
{
    Fixture fixture;
    fixture.store->blockLoad = true;
    std::exception_ptr failure;
    std::jthread loading(
        [&]
        {
            try
            {
                fixture.cache->SetLeague("A");
            }
            catch (...)
            {
                failure = std::current_exception();
            }
        }
    );
    fixture.store->WaitForLoad();
    fixture.cache->ForceRefreshAsync();
    fixture.provider->WaitForCalls(1);
    fixture.provider->Respond(FreshPrices());
    fixture.Wait();
    const auto version = fixture.cache->Version();
    fixture.store->ReleaseLoad();
    loading.join();
    if (failure)
        std::rethrow_exception(failure);
    Require(
        fixture.cache->GetPrice("Runic Alloy") == 30.0 && fixture.cache->DivineRate() == 200.0,
        "late disk read replaced fresh network prices"
    );
    Require(fixture.cache->Version() == version, "obsolete disk read was published");
}

void TestCorruptJson()
{
    for (const char* content : { "{broken", "[]", R"({"items": []})", R"({"items": {"Runic Alloy": "bad"}})" })
    {
        const auto path = GetUserDataDir() / "prices_dump_Broken.json";
        {
            std::ofstream file(path);
            file << content;
            Require(file.good(), "could not write test dump");
        }
        Fixture fixture;
        fixture.cache = std::make_unique<PriceCache>(std::make_unique<FakeProvider>(fixture.provider), CreatePriceStore());
        fixture.Begin("Broken");
        Require(fixture.cache->GetPriceCount() == 0, "corrupt JSON supplied a price");
        fixture.provider->Respond(FreshPrices());
        fixture.Wait();
        JsonPriceStore store;
        const auto recovered = store.Load("Broken");
        Require(recovered && recovered->items.at("Runic Alloy").ex == 30.0, "corrupt dump was not replaced after recovery");
    }
}
}

int main()
{
    std::string temporary = (std::filesystem::temp_directory_path() / "runehelper-price-tests-XXXXXX").string();
    if (!mkdtemp(temporary.data()))
        return 1;
    if (setenv("XDG_CONFIG_HOME", temporary.c_str(), 1) != 0)
    {
        std::filesystem::remove_all(temporary);
        return 1;
    }

    int failures = 0;
    const std::pair<const char*, void (*)()> cases[] = {
        { "successful refresh", TestSuccessfulRefresh },
        { "network failure and recovery", TestNetworkFailureAndRecovery },
        { "partial refresh", TestPartialRefresh },
        { "store read failure", TestStoreReadFailure },
        { "store write failure", TestStoreWriteFailure },
        { "league switch", TestLeagueSwitch },
        { "stale failure", TestStaleFailure },
        { "switch during save", TestSwitchDuringSave },
        { "cancellation", TestCancellation },
        { "late store read", TestLateStoreRead },
        { "corrupt JSON", TestCorruptJson },
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
