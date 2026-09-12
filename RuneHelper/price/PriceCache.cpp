#include "PriceCache.h"

#include "core/Logger.h"
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

        ~RefreshGuard()
        {
            flag.store(false);
        }
    };

    std::string DumpFileNameForLeague(const std::string& league)
    {
        std::string suffix;
        suffix.reserve(league.size());

        for (unsigned char ch : league)
        {
            if ((ch >= 'A' && ch <= 'Z') ||
                (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9'))
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
}

PriceCache::PriceCache() : provider_(std::make_unique<PoeNinjaPriceProvider>())
{
    LOG_INFO("PriceCache::PriceCache() -> init");
    LoadDump();
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

std::optional<std::string> PriceCache::GetPrice(const std::string& itemName)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = prices_.find(itemName);

    if (it == prices_.end())
        return std::nullopt;

    return it->second.price;
}

void PriceCache::RefreshIfNeeded()
{
    int64_t now = NowUnix();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!prices_.empty() && now - dump_updated_at_ < refresh_seconds_)
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

    std::lock_guard<std::mutex> lock(refreshThreadMutex_);

    refreshThread_ = std::jthread([this](std::stop_token stop) { RefreshWorker(stop); });
}

void PriceCache::SetRefreshMinutes(int minutes)
{
    const int clampedMinutes = std::clamp(minutes, 5, 360);
    std::lock_guard<std::mutex> lock(mutex_);
    refresh_seconds_ = clampedMinutes * 60;
}

void PriceCache::SetLeague(std::string league)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (league_ == league)
            return;

        league_ = std::move(league);
        prices_.clear();
        dump_updated_at_ = 0;
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

    if (fresh.empty())
    {
        LOG_ERROR("PriceCache::RefreshWorker() -> refresh failed or empty");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (league_ != league)
        {
            LOG_INFO("PriceCache::RefreshWorker() -> stale league refresh ignored");
            return;
        }

        prices_ = std::move(fresh);
        dump_updated_at_ = now;
        ++version_;
    }

    SaveDump();

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
        for (const auto& [name, info] : prices_)
            j["items"][name] = info.price;
    }

    std::ofstream file(DumpPathForLeague(league));

    if (!file)
    {
        LOG_ERROR("PriceCache::SaveDump() -> failed to open file");
        return;
    }

    file << j.dump(4);

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
        if (!it.value().is_string())
            continue;

        loaded[it.key()] = PriceInfo{it.value().get<std::string>()};
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (league_ != league)
            return;

        prices_ = std::move(loaded);
        dump_updated_at_ = j.value("dump_updated_at", 0LL);
        ++version_;
    }

    LOG_INFO("Loaded dump prices -> " + std::to_string(GetPriceCount()));
    LOG_INFO("PriceCache::LoadDump() -> return");
}
