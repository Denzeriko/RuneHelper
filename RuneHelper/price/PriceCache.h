#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "price/PriceProvider.h"

class PriceCache
{
public:
    PriceCache();
    ~PriceCache();

    void RefreshIfNeeded();
    void ForceRefreshAsync();
    void SetRefreshMinutes(int minutes);
    void SetLeague(std::string league);

    bool IsRefreshInProgress() const;
    size_t GetPriceCount() const;
    std::uint64_t Version() const;

    std::optional<std::string> GetPrice(const std::string& itemName);
    std::vector<std::string> GetAllItemNames() const;

private:
    static int64_t NowUnix();

    void RefreshWorker(const std::stop_token& stop);

    void LoadDump();
    void SaveDump();

private:
    mutable std::mutex mutex_;

    std::unordered_map<std::string, PriceInfo> prices_;

    std::uint64_t version_ = 0;
    int64_t dump_updated_at_ = 0;
    int64_t refresh_seconds_ = 60 * 60;
    std::string league_ = "Forbidden Rites";
    std::unique_ptr<PriceProvider> provider_;

    std::atomic<bool> refreshInProgress_ = false;

    std::mutex refreshThreadMutex_;
    std::jthread refreshThread_;
};
