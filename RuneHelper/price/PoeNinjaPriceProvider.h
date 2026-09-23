#pragma once

#include "price/PriceProvider.h"

#include <stop_token>
#include <string>

#include <cpr/cpr.h>

#include "nlohmann/json.hpp"

class PoeNinjaPriceProvider final : public PriceProvider
{
public:
    PriceTable DownloadPrices(const std::string& league, const std::stop_token& stop) override;

    static PriceTable ParseCategoryDump(const nlohmann::json& j);

private:
    static std::string EncodeUrlComponent(const std::string& text);

    PriceTable DownloadCategory(
        cpr::Session& session,
        const std::string& encodedLeague,
        const std::string& type,
        const std::stop_token& stop
    );
};
