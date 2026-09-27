#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "price/PriceProvider.h"

struct PriceDump
{
    std::unordered_map<std::string, PriceInfo> items;
    double divineToEx = 0.0;
    std::int64_t updatedAt = 0;
};

class PriceStore
{
public:
    virtual ~PriceStore() = default;
    virtual std::optional<PriceDump> Load(const std::string& league) = 0;
    virtual bool Save(const std::string& league, const PriceDump& dump) = 0;
};

class JsonPriceStore final : public PriceStore
{
public:
    std::optional<PriceDump> Load(const std::string& league) override;
    bool Save(const std::string& league, const PriceDump& dump) override;
};

std::unique_ptr<PriceStore> CreatePriceStore();
