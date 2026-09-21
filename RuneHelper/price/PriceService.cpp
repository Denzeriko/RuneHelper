#include "price/PriceService.h"

#include <chrono>

namespace
{
constexpr std::chrono::seconds kRefreshCheckInterval{ 10 };
}

void PriceService::Apply(const AppConfig& config)
{
    cache_.SetRefreshMinutes(config.priceRefreshMinutes);
    cache_.SetLeague(config.priceLeague);

    if (config.priceSearchEnabled)
        cache_.RefreshIfNeeded();
}

void PriceService::Tick(const AppConfig& config)
{
    cache_.SetRefreshMinutes(config.priceRefreshMinutes);
    cache_.SetLeague(config.priceLeague);

    if (!config.priceSearchEnabled)
        return;

    const auto now = std::chrono::steady_clock::now();

    if (now - lastRefreshCheck_ < kRefreshCheckInterval)
        return;

    lastRefreshCheck_ = now;
    cache_.RefreshIfNeeded();
}

void PriceService::ForceRefresh()
{
    cache_.ForceRefreshAsync();
}

PriceStatus PriceService::Status() const
{
    return { cache_.IsRefreshInProgress(), cache_.GetPriceCount() };
}

void PriceService::RebuildNames()
{
    const std::uint64_t version = cache_.Version();

    std::lock_guard lock(namesMutex_);

    if (names_ && namesVersion_ == version)
        return;

    names_ = std::make_shared<const CachedItemNames>(
        CachedItemNames::Build(cache_.GetAllItemNames()));

    namesVersion_ = version;
}

ResolvedPrice PriceService::Resolve(const std::string& rawName, int quantity)
{
    ResolvedPrice resolved;
    resolved.name = rawName;

    auto price = cache_.GetPrice(rawName);

    if (price)
    {
        resolved.confidence = 100;
    }
    else
    {
        RebuildNames();

        std::shared_ptr<const CachedItemNames> names;
        {
            std::lock_guard lock(namesMutex_);
            names = names_;
        }

        if (names)
        {
            if (auto guess = names->FindBest(rawName))
            {
                resolved.name = guess->name;
                resolved.confidence = guess->confidence;
                price = cache_.GetPrice(guess->name);
            }
        }
    }

    if (price)
        resolved.totalEx = *price * quantity;

    resolved.unitEx = price;

    return resolved;
}

double PriceService::DivineRate() const
{
    return cache_.DivineRate();
}
