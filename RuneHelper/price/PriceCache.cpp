#include "PriceCache.h"

#include "core/Helpers.h"
#include "core/Logger.h"

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

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!prices_.empty() &&
            now - dump_updated_at_ < refresh_seconds_)
        {
            return;
        }
    }

    ForceRefreshAsync();
}

void PriceCache::ForceRefreshAsync()
{
    bool expected = false;

    if (!refreshInProgress_.compare_exchange_strong(expected, true))
    {
        LOG_INFO("Price refresh already in progress");
        return;
    }

    std::thread([this]()
        {
            RefreshWorker();
        }).detach();
}

bool PriceCache::IsRefreshInProgress() const
{
    return refreshInProgress_.load();
}

size_t PriceCache::GetPriceCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return prices_.size();
}

void PriceCache::RefreshWorker()
{
    LOG_INFO("PriceCache::RefreshWorker() -> start");

    int64_t now = NowUnix();

    auto fresh = DownloadFullDump();

    if (fresh.empty())
    {
        LOG_ERROR("PriceCache refresh failed or empty");
        refreshInProgress_ = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        prices_ = std::move(fresh);
        dump_updated_at_ = now;
    }

    SaveDump();

    refreshInProgress_ = false;

    LOG_INFO("PriceCache::RefreshWorker() -> done");
}

int64_t PriceCache::NowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::unordered_map<std::string, PriceInfo> PriceCache::DownloadFullDump()
{
    LOG_INFO("PriceCache::DownloadFullDump() -> poe.ninja");
    auto result = DownloadPoeNinjaDump("Runes");
    auto currency = DownloadPoeNinjaDump("Currency");

    for (auto& [name, info] : currency)
        result[name] = std::move(info);

    LOG_INFO("PriceCache::DownloadFullDump() -> total prices: " + std::to_string(result.size()));

    return result;
}

std::unordered_map<std::string, PriceInfo> PriceCache::DownloadPoeNinjaDump(const std::string& type)
{
    const std::string url = "https://poe.ninja/poe2/api/economy/exchange/current/overview?league=Runes+of+Aldur&type=" + type;

    LOG_INFO("PriceCache::DownloadPoeNinjaDump() -> " + url);

    auto r = cpr::Get(
        cpr::Url{ url },
        cpr::Header{
            { "User-Agent", "RuneHelper/1.0" },
            { "Accept", "application/json" },
            { "Referer", "https://poe.ninja/poe2/economy/runesofaldur/" }
        },
        cpr::Timeout{ 15000 }
    );

    LOG_INFO("PriceCache::DownloadPoeNinjaDump() HTTP: " + std::to_string(r.status_code) + " bytes=" + std::to_string(r.text.size()));

    if (r.error.code != cpr::ErrorCode::OK)
    {
        LOG_ERROR("PriceCache poe.ninja CPR error: code=" + std::to_string(static_cast<int>(r.error.code)) + " message=" + r.error.message);
        return {};
    }

    if (r.status_code != 200)
    {
        LOG_ERROR("PriceCache poe.ninja HTTP error: " + std::to_string(r.status_code));
        return {};
    }

    json j = json::parse(r.text, nullptr, false);

    if (j.is_discarded())
    {
        LOG_ERROR("PriceCache poe.ninja JSON parse failed");

        return {};
    }

    return ParsePoeNinjaDump(j);
}

std::unordered_map<std::string, PriceInfo> PriceCache::ParsePoeNinjaDump(const json& j)
{
    std::unordered_map<std::string, PriceInfo> result;

    if (!j.contains("core") || !j["core"].contains("rates") || !j.contains("items") || !j["items"].is_array() || !j.contains("lines") || !j["lines"].is_array())
    {
        LOG_ERROR("PriceCache::ParsePoeNinjaDump() invalid JSON structure");
        return result;
    }

    double divineToEx = j["core"]["rates"].value("exalted", 0.0);

    if (divineToEx <= 0.0)
    {
        LOG_ERROR("PriceCache::ParsePoeNinjaDump() invalid exalted rate");
        return result;
    }

    std::unordered_map<std::string, std::string> idToName;

    for (const auto& item : j["items"])
    {
        std::string id = item.value("id", "");
        std::string name = item.value("name", "");

        if (!id.empty() && !name.empty())
            idToName[id] = name;
    }

    for (const auto& line : j["lines"])
    {
        std::string id = line.value("id", "");

        if (id.empty())
            continue;

        auto it = idToName.find(id);
        if (it == idToName.end())
            continue;

        double primaryValue =
            line.value("primaryValue", 0.0);

        if (primaryValue <= 0.0)
            continue;

        double exValue =
            primaryValue * divineToEx;

        result[it->second] = PriceInfo{
            FormatExPrice(exValue)
        };
    }

    LOG_INFO("PriceCache::ParsePoeNinjaDump() parsed prices: " + std::to_string(result.size()));

    return result;
}

std::string PriceCache::FormatExPrice(double value)
{
    std::ostringstream ss;

    if (value >= 100.0)
    {
        ss << std::fixed << std::setprecision(0);
    }
    else if (value >= 10.0)
    {
        ss << std::fixed << std::setprecision(1);
    }
    else
    {
        ss << std::fixed << std::setprecision(2);
    }

    ss << value << " ex";

    return ss.str();
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