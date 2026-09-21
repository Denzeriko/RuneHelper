#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "core/Config.h"
#include "ocr/NameNormalizer.h"
#include "price/PriceCache.h"
#include "price/ResolvedPrice.h"

struct PriceStatus
{
    bool downloading = false;
    std::size_t priceCount = 0;
};

class PriceService
{
public:
    void Apply(const AppConfig& config);
    void Tick(const AppConfig& config);
    void ForceRefresh();

    PriceStatus Status() const;

    ResolvedPrice Resolve(const std::string& rawName, int quantity);

    double DivineRate() const;

private:
    void RebuildNames();

    PriceCache cache_;

    std::mutex namesMutex_;
    std::shared_ptr<const CachedItemNames> names_;
    std::uint64_t namesVersion_ = 0;

    std::chrono::steady_clock::time_point lastRefreshCheck_{};
};
