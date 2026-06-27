#pragma once

#include <unordered_map>
#include <optional>
#include <string>
#include <mutex>

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
private:
    std::unordered_map<std::string, PriceInfo> prices_;
    std::mutex mutex_;

    int64_t dump_updated_at_ = 0;
    const int64_t refresh_seconds_ = 15 * 60; //15 minutes

    static int64_t NowUnix();
    std::unordered_map<std::string, PriceInfo> DownloadFullDump();

    void SaveDump();
    void LoadDump();
};