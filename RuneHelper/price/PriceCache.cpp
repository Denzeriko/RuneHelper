#include "PriceCache.h"

#include "core/AtomicFile.h"
#include "core/Logger.h"
#include "core/ThreadGuard.h"
#include "platform/PlatformPaths.h"
#include "price/PoeNinjaPriceProvider.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace
{
struct RefreshGuard
{
    std::atomic<bool>& flag;

    ~RefreshGuard() { flag.store(false); }
};

std::string DumpFileNameForLeague(const std::string& league)
{
    std::string suffix;
    suffix.reserve(league.size());

    for (unsigned char ch : league)
    {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
        {
            suffix.push_back(static_cast<char>(ch));
        }
        else if (suffix.empty() || suffix.back() != '_')
        {
            suffix.push_back('_');
        }
    }

    while (!suffix.empty() && suffix.back() == '_')
        suffix.pop_back();

    if (suffix.empty())
        suffix = "unknown";

    return "prices_dump_" + suffix + ".json";
}

std::filesystem::path DumpPathForLeague(const std::string& league)
{
    return GetUserDataDir() / DumpFileNameForLeague(league);
}

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

PriceCache::PriceCache() : provider_(std::make_unique<PoeNinjaPriceProvider>()) {}

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

        if (!prices_.empty() && now - dump_updated_at_ < refresh_seconds_)
            return;

        if (now - last_failure_at_ < BackoffSeconds(failure_streak_))
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
    const int clampedMinutes = std::clamp(minutes, 5, 360);
    std::lock_guard<std::mutex> lock(mutex_);
    refresh_seconds_ = static_cast<int64_t>(clampedMinutes) * 60;
}

void PriceCache::SetLeague(std::string league)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (league_ == league)
            return;

        league_ = std::move(league);
        prices_.clear();
        divineToEx_ = 0.0;
        dump_updated_at_ = 0;
        last_failure_at_ = 0;
        failure_streak_ = 0;
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

std::uint64_t PriceCache::Version() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

void PriceCache::RefreshWorker(const std::stop_token& stop)
{
    RefreshGuard guard{ refreshInProgress_ };
    LOG_INFO("PriceCache::RefreshWorker() -> start");

    int64_t now = NowUnix();
    std::string league;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        league = league_;
    }

    auto fresh = provider_->DownloadPrices(league, stop);

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
            ++failure_streak_;
            last_failure_at_ = NowUnix();
            attempt = failure_streak_;
            retryIn = BackoffSeconds(failure_streak_);
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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (league_ != league)
        {
            LOG_INFO("PriceCache::RefreshWorker() -> stale league refresh ignored");
            return;
        }

        if (partial)
        {
            for (const auto& [name, info] : fresh.items)
                prices_[name] = info;

            ++failure_streak_;
            last_failure_at_ = NowUnix();
            attempt = failure_streak_;
            retryIn = BackoffSeconds(failure_streak_);
        }
        else
        {
            prices_ = std::move(fresh.items);
            dump_updated_at_ = now;
            last_failure_at_ = 0;
            failure_streak_ = 0;
        }

        if (fresh.divineToEx > 0.0)
            divineToEx_ = fresh.divineToEx;

        ++version_;
    }

    SaveDump();

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

void PriceCache::SaveDump()
{
    LOG_INFO("PriceCache::SaveDump() -> call");

    json j;
    j["items"] = json::object();

    std::string league;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        league = league_;
        j["league"] = league;
        j["dump_updated_at"] = dump_updated_at_;
        j["divine_to_ex"] = divineToEx_;
        for (const auto& [name, info] : prices_)
            j["items"][name] = info.ex;
    }

    if (!WriteFileAtomic(DumpPathForLeague(league), j.dump(4)))
    {
        LOG_ERROR("PriceCache::SaveDump() -> failed to write file");
        return;
    }

    LOG_INFO("PriceCache::SaveDump() -> return");
}

void PriceCache::LoadDump()
{
    LOG_INFO("PriceCache::LoadDump() -> call");

    std::string league;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        league = league_;
    }

    std::ifstream file(DumpPathForLeague(league));
    if (!file)
        return;

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded())
    {
        LOG_ERROR("PriceCache::LoadDump() -> JSON parse failed");
        return;
    }

    if (!j.contains("items") || !j["items"].is_object())
        return;

    std::unordered_map<std::string, PriceInfo> loaded;
    for (auto it = j["items"].begin(); it != j["items"].end(); ++it)
    {
        if (!it.value().is_number())
            continue;

        loaded[it.key()] = PriceInfo{ it.value().get<double>() };
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (league_ != league)
            return;

        prices_ = std::move(loaded);
        divineToEx_ = j.value("divine_to_ex", 0.0);
        dump_updated_at_ = j.value("dump_updated_at", 0LL);
        ++version_;
    }

    LOG_INFO("Loaded dump prices -> " + std::to_string(GetPriceCount()));
    LOG_INFO("PriceCache::LoadDump() -> return");
}
