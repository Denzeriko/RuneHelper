#pragma once

#include "price/PriceProvider.h"

#include <stop_token>
#include <string>
#include <unordered_map>

#include "nlohmann/json.hpp"

class PoeNinjaPriceProvider final : public PriceProvider
{
public:
    std::unordered_map<std::string, PriceInfo> DownloadPrices(const std::string& league, const std::stop_token& stop) override;

private:
    static std::string EncodeUrlComponent(const std::string& text);
    static std::string FormatExPrice(double value);

    std::unordered_map<std::string, PriceInfo> DownloadCategory(const std::string& encodedLeague, const std::string& type, const std::stop_token& stop);
    std::unordered_map<std::string, PriceInfo> ParseCategoryDump(const nlohmann::json& j);
};
