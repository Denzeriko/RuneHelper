#include "PriceCache.h"

#include "Helpers.h"
#include "Logger.h"

#include <cpr/cpr.h>
#include "nlohmann/json.hpp"

#include <unordered_map>
#include <string>
#include <fstream>
#include <chrono>
#include <optional>
#include <mutex>
#include <iostream>
#include <regex>

using json = nlohmann::json;

std::string JsonString(const json& j, const char* key)
{
    if (!j.contains(key))
        return "";

    const auto& v = j.at(key);

    if (v.is_null())
        return "";

    if (v.is_string())
        return v.get<std::string>();

    return "";
}

PriceCache::PriceCache()
{
    LOG_INFO("PriceCache::PriceCache() INIT");

    LoadDump();
    RefreshIfNeeded();
}

std::vector<std::string> PriceCache::GetAllItemNames() const
{
    std::vector<std::string> result;
    result.reserve(prices_.size());

    for (const auto& [name, info] : prices_)
        result.push_back(name);

    return result;
}

std::optional<std::string> PriceCache::GetPrice(const std::string& itemName)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = prices_.find(itemName);
    if (it == prices_.end())
        return std::nullopt;

    return it->second.price;
}

const std::unordered_map<std::string, PriceInfo>& PriceCache::GetPrices() const
{
    return prices_;
}

void PriceCache::RefreshIfNeeded()
{
    LOG_INFO("PriceCache::RefreshIfNeeded() -> call");

    int64_t now = NowUnix();

    if (!prices_.empty() && now - dump_updated_at_ < refresh_seconds_)
        return;

    auto fresh = DownloadFullDump();

    if (fresh.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        prices_ = std::move(fresh);
        dump_updated_at_ = now;
    }

    LOG_INFO("PriceCache::RefreshIfNeeded() -> return");

    SaveDump();

    LOG_INFO("PriceCache::RefreshIfNeeded() -> SaveDump() -> return");
}

int64_t PriceCache::NowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::unordered_map<std::string, PriceInfo>  PriceCache::DownloadFullDump()
{
    LOG_INFO("PriceCache::DownloadFullDump() -> call");

    std::unordered_map<std::string, PriceInfo> result;

    const std::string url = "https://poe2scout.com/api/poe2/Leagues/runes/Items";

    auto r = cpr::Get(
        cpr::Url{ url },
        cpr::Header{
            { "User-Agent", "RuneHelper/1.0" },
            { "Accept", "application/json" }
        },
        cpr::Timeout{ 15000 },
        cpr::VerifySsl{ false }
    );

    LOG_INFO("PriceCache::DownloadFullDump() -> HTTP: " + std::to_string(r.status_code));

    if (r.error.code != cpr::ErrorCode::OK)
    {
        LOG_ERROR(
            "PriceCache CPR error: code=" +
            std::to_string(static_cast<int>(r.error.code)) +
            " message=" + r.error.message +
            " http=" + std::to_string(r.status_code) +
            " bytes=" + std::to_string(r.text.size())
        );
        return {};
    }

    if (r.status_code != 200)
    {
        LOG_ERROR("PriceCache API HTTP error:  " + std::to_string(r.status_code));
        return {};
    }

    json j = json::parse(r.text, nullptr, false);

    if (j.is_discarded() || !j.is_array())
    {
        LOG_ERROR("API JSON parse error");
        return {};
    }

    for (const auto& item : j)
    {
        std::string text = JsonString(item, "Text");
        std::string name = JsonString(item, "Name");
        std::string type = JsonString(item, "Type");

        double price = 0.0;

        if (item.contains("CurrentPrice") &&
            item["CurrentPrice"].is_number())
        {
            price = item["CurrentPrice"].get<double>();
        }

        //if (price <= 0.0) //do we need to see 0.0? or make placeholder with poop emoji
        //    continue;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << price << " ex";

        PriceInfo info{ ss.str() };

        if (!text.empty())
            result[text] = info;

        if (!name.empty())
            result[name] = info;

        if (!name.empty() && !type.empty())
            result[name + " " + type] = info;
    }

    LOG_INFO("Loaded prices:  " + std::to_string(result.size()));
    return result;
}

void PriceCache::SaveDump()
{
    LOG_INFO("PriceCache::SaveDump() -> call");

    json j;

    j["dump_updated_at"] = dump_updated_at_;
    j["items"] = json::object();

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [name, info] : prices_)
    {
        j["items"][name] = info.price;
    }

    std::ofstream file((GetAppDataDir() / "prices_dump.json"));
    if (file)
        file << j.dump(4);

    LOG_INFO("PriceCache::SaveDump() -> return");
}

void PriceCache::LoadDump()
{
    LOG_INFO("PriceCache::LoadDump() -> call");

    std::ifstream file((GetAppDataDir() / "prices_dump.json"));
    if (!file)
        return;

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded())
        return;

    dump_updated_at_ = j.value("dump_updated_at", 0LL);

    if (!j.contains("items") || !j["items"].is_object())
        return;

    for (auto it = j["items"].begin(); it != j["items"].end(); ++it)
    {
        if (!it.value().is_string())
            continue;

        prices_[it.key()] = PriceInfo{
            it.value().get<std::string>()
        };
    }

    LOG_INFO("Loaded dump prices -> " + std::to_string(prices_.size()));
    LOG_INFO("PriceCache::LoadDump() -> return");
}