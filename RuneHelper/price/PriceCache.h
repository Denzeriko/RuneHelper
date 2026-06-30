#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

struct PriceInfo
{
    std::string price;
};

class PriceCache
{
public:
    PriceCache();

    void RefreshIfNeeded();
    void ForceRefreshAsync();

    bool IsRefreshInProgress() const;
    size_t GetPriceCount() const;

    std::optional<std::string> GetPrice(const std::string& itemName);
    std::vector<std::string> GetAllItemNames() const;

private:
    static int64_t NowUnix();

    void RefreshWorker();

    std::unordered_map<std::string, PriceInfo> DownloadFullDump();
    std::unordered_map<std::string, PriceInfo> DownloadPoeNinjaDump(const std::string& type);
    std::unordered_map<std::string, PriceInfo> ParsePoeNinjaDump(const nlohmann::json& j);

    static std::string FormatExPrice(double value);

    void LoadDump();
    void SaveDump();

private:
    mutable std::mutex mutex_;

    std::unordered_map<std::string, PriceInfo> prices_;

    int64_t dump_updated_at_ = 0;
    int64_t refresh_seconds_ = 60 * 60;

    std::atomic<bool> refreshInProgress_ = false;
};
