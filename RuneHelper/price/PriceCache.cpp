#include "PriceCache.h"

#include "core/Config.h"
#include "common/ExceptionLogging.h"
#include "common/Logger.h"
#include "price/PoeNinjaPriceProvider.h"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
struct RefreshGuard
{
    std::atomic<bool>& flag;

    ~RefreshGuard() { flag.store(false); }
};

int64_t BackoffSeconds(int failureStreak)
{
    if (failureStreak <= 0)
        return 0;

    constexpr int64_t kFirstRetrySeconds = 30;
    constexpr int64_t kMaxRetrySeconds = 30LL * 60;

    const int shift = std::min(failureStreak - 1, 10);

    return std::min<int64_t>(kFirstRetrySeconds << shift, kMaxRetrySeconds);
}
}

PriceCache::PriceCache(std::unique_ptr<PriceProvider> provider, std::unique_ptr<PriceStore> store)
    : provider_(provider ? std::move(provider) : std::make_unique<PoeNinjaPriceProvider>()),
      store_(store ? std::move(store) : CreatePriceStore())
{
}

PriceCache::~PriceCache()
{
    std::lock_guard<std::mutex> lock(refreshThreadMutex_);

    if (refreshThread_.joinable())
        refreshThread_.request_stop();
}

std::vector<std::string> PriceCache::GetAllItemNames() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(prices_.size());

    for (const auto& [name, info] : prices_)
        result.push_back(name);

    return result;
}

std::optional<double> PriceCache::GetPrice(const std::string& itemName)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = prices_.find(itemName);

    if (it == prices_.end())
        return std::nullopt;

    return it->second.ex;
}

double PriceCache::DivineRate() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return divineToEx_;
}

void PriceCache::RefreshIfNeeded()
{
    int64_t now = NowUnix();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!prices_.empty() && now - dumpUpdatedAt_ < refreshSeconds_)
            return;

        if (now - lastFailureAt_ < BackoffSeconds(failureStreak_))
            return;
    }

    ForceRefreshAsync();
}

void PriceCache::ForceRefreshAsync()
{
    bool expected = false;

    if (!refreshInProgress_.compare_exchange_strong(expected, true))
    {
        LOG_INFO("PriceCache::ForceRefreshAsync() -> already in progress");
        return;
    }

    std::jthread previous;

    {
        std::lock_guard<std::mutex> lock(refreshThreadMutex_);

        previous = std::move(refreshThread_);

        refreshThread_ = std::jthread([this](const std::stop_token& stop)
                                      { RunLoggingExceptions("PriceCache refresh thread", [&] { RefreshWorker(stop); }); });
    }
}

void PriceCache::SetRefreshMinutes(int minutes)
{
    const int clampedMinutes = std::clamp(minutes, kMinPriceRefreshMinutes, kMaxPriceRefreshMinutes);
    std::lock_guard<std::mutex> lock(mutex_);
    refreshSeconds_ = static_cast<int64_t>(clampedMinutes) * 60;
}

void PriceCache::SetLeague(std::string league)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (league_ == league)
            return;

        league_ = std::move(league);
        ++leagueVersion_;
        prices_.clear();
        divineToEx_ = 0.0;
        dumpUpdatedAt_ = 0;
        lastFailureAt_ = 0;
        failureStreak_ = 0;
        ++version_;
    }

    LoadDump();
}

bool PriceCache::IsRefreshInProgress() const
{
    return refreshInProgress_.load();
}

size_t PriceCache::GetPriceCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return prices_.size();
}

PriceStatus PriceCache::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return { refreshInProgress_.load(), prices_.size(), dumpUpdatedAt_, failureStreak_ > 0 };
}

std::uint64_t PriceCache::Version() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

void PriceCache::RefreshWorker(const std::stop_token& stop)
{
    RefreshGuard guard{ refreshInProgress_ };
    LOG_INFO("PriceCache::RefreshWorker() -> start");

    std::string league;
    std::uint64_t leagueVersion = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        league = league_;
        leagueVersion = leagueVersion_;
    }

    PriceTable fresh;
    RunLoggingExceptions("PriceCache download", [&] { fresh = provider_->DownloadPrices(league, stop); });

    if (stop.stop_requested())
    {
        LOG_INFO("PriceCache::RefreshWorker() -> cancelled");
        return;
    }

    if (fresh.items.empty())
    {
        int64_t retryIn = 0;
        int attempt = 0;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (leagueVersion_ != leagueVersion)
                return;

            ++failureStreak_;
            lastFailureAt_ = NowUnix();
            attempt = failureStreak_;
            retryIn = BackoffSeconds(failureStreak_);
        }

        LOG_ERROR(
            "PriceCache::RefreshWorker() -> refresh failed or empty, attempt " + std::to_string(attempt) + ", next try in " +
            std::to_string(retryIn) + "s"
        );

        return;
    }

    const bool partial = !fresh.complete;
    int64_t retryIn = 0;
    int attempt = 0;
    PriceDump dump;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (leagueVersion_ != leagueVersion)
        {
            LOG_INFO("PriceCache::RefreshWorker() -> stale league refresh ignored");
            return;
        }

        if (partial)
        {
            for (const auto& [name, info] : fresh.items)
                prices_[name] = info;

            ++failureStreak_;
            lastFailureAt_ = NowUnix();
            attempt = failureStreak_;
            retryIn = BackoffSeconds(failureStreak_);
        }
        else
        {
            prices_ = std::move(fresh.items);
            dumpUpdatedAt_ = NowUnix();
            lastFailureAt_ = 0;
            failureStreak_ = 0;
        }

        if (fresh.divineToEx > 0.0)
            divineToEx_ = fresh.divineToEx;

        ++version_;
        dump = { prices_, divineToEx_, dumpUpdatedAt_ };
    }

    if (!store_->Save(league, dump))
        LOG_ERROR("PriceCache::RefreshWorker() -> failed to save prices");

    if (partial)
    {
        LOG_ERROR(
            "PriceCache::RefreshWorker() -> partial refresh merged, kept the previous prices for the "
            "categories that failed, attempt " +
            std::to_string(attempt) + ", next try in " + std::to_string(retryIn) + "s"
        );

        return;
    }

    LOG_INFO("PriceCache::RefreshWorker() -> done");
}

int64_t PriceCache::NowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void PriceCache::LoadDump()
{
    std::string league;
    std::uint64_t version = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        league = league_;
        version = version_;
    }

    std::optional<PriceDump> dump;
    RunLoggingExceptions("PriceCache load dump", [&] { dump = store_->Load(league); });

    if (!dump)
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (version_ != version)
            return;

        prices_ = std::move(dump->items);
        divineToEx_ = dump->divineToEx;
        dumpUpdatedAt_ = dump->updatedAt;
        ++version_;
    }

    LOG_INFO("Loaded dump prices -> " + std::to_string(GetPriceCount()));
}
