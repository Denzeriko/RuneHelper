#pragma once

#include <unordered_map>
#include <optional>
#include <string>
#include <mutex>

#include "nlohmann/json.hpp"

struct PriceInfo
{
    std::string price;
};

class PriceCache
{
public:
    PriceCache();
    std::vector<std::string> GetAllItemNames() const;
    std::optional<std::string> GetPrice(const std::string& itemName);
    const std::unordered_map<std::string, PriceInfo>& GetPrices() const;
    void RefreshIfNeeded();
    void ForceRefreshAsync();
    bool IsRefreshInProgress() const;
    size_t GetPriceCount() const;

private:
    void RefreshWorker();
    std::atomic<bool> refreshInProgress_ = false;

    std::unordered_map<std::string, PriceInfo> prices_;
    mutable std::mutex mutex_;

    int64_t dump_updated_at_ = 0;
    const int64_t refresh_seconds_ = 15 * 60; //15 minutes

    static int64_t NowUnix();
    std::unordered_map<std::string, PriceInfo> DownloadFullDump();

    std::unordered_map<std::string, PriceInfo> DownloadPoeNinjaDump(const std::string& type);
    std::unordered_map<std::string, PriceInfo> ParsePoeNinjaDump(const nlohmann::json& j);

    static std::string FormatExPrice(double value);

    void SaveDump();
    void LoadDump();
};