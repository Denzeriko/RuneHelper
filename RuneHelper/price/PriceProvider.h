#pragma once

#include <stop_token>
#include <string>
#include <unordered_map>

struct PriceInfo
{
    double ex = 0.0;
};

struct PriceTable
{
    std::unordered_map<std::string, PriceInfo> items;
    double divineToEx = 0.0;
    bool complete = true;
};

class PriceProvider
{
public:
    virtual ~PriceProvider() = default;

    virtual PriceTable DownloadPrices(const std::string& league, const std::stop_token& stop) = 0;
};
