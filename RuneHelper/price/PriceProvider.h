#pragma once

#include <stop_token>
#include <string>
#include <unordered_map>

struct PriceInfo
{
    std::string price;
};

class PriceProvider
{
public:
    virtual ~PriceProvider() = default;

    virtual std::unordered_map<std::string, PriceInfo> DownloadPrices(const std::string& league, const std::stop_token& stop) = 0;
};
