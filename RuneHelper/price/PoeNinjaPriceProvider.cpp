#include "price/PoeNinjaPriceProvider.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

#include <cpr/cpr.h>

#include "core/JsonRead.h"
#include "core/Logger.h"

using json = nlohmann::json;

namespace
{
const std::string& PriceApiBase()
{
    static const std::string base = []
    {
        if (const char* override = std::getenv("RUNEHELPER_PRICE_API"); override && *override)
            return std::string(override);

        return std::string("https://denz.pw/poe2/economy");
    }();

    return base;
}

const std::string& UserAgent()
{
    static const std::string agent = std::string("RuneHelper/") + RUNEHELPER_VERSION + " (+https://github.com/Denzeriko/RuneHelper)";

    return agent;
}

constexpr const char* kDirectApi = "https://poe.ninja/poe2/api/economy/exchange/current/overview";
constexpr int kProxyFailureLimit = 3;

std::atomic<int>& ProxyFailures()
{
    static std::atomic<int> failures{ 0 };
    return failures;
}

void ConfigureSession(cpr::Session& session, const std::stop_token& stop)
{
    session.SetHeader(cpr::Header{ { "User-Agent", UserAgent() }, { "Accept", "application/json" } });

    session.SetTimeout(cpr::Timeout{ 15000 });

    session.SetAcceptEncoding(cpr::AcceptEncoding{ { cpr::AcceptEncodingMethods::gzip, cpr::AcceptEncodingMethods::deflate } });

    session.SetProgressCallback(cpr::ProgressCallback{ [&stop](auto, auto, auto, auto, std::intptr_t)
                                                       { return !stop.stop_requested(); } });
}

bool Fetch(cpr::Session& session, const std::string& url, std::string& body, const std::stop_token& stop)
{
    LOG_INFO("PoeNinjaPriceProvider::DownloadCategory() -> " + url);

    session.SetUrl(cpr::Url{ url });

    cpr::Response r = session.Get();

    if (stop.stop_requested())
        return false;

    LOG_INFO(
        "PoeNinjaPriceProvider::DownloadCategory() HTTP: " + std::to_string(r.status_code) + " bytes=" + std::to_string(r.text.size())
    );

    if (r.error.code != cpr::ErrorCode::OK)
    {
        LOG_ERROR(
            "PoeNinjaPriceProvider CPR error: code=" + std::to_string(static_cast<int>(r.error.code)) + " message=" + r.error.message
        );
        return false;
    }

    if (r.status_code != 200)
    {
        LOG_ERROR("PoeNinjaPriceProvider HTTP error: " + std::to_string(r.status_code));
        return false;
    }

    body = std::move(r.text);
    return true;
}

PriceTable FailedTable()
{
    PriceTable table;
    table.complete = false;
    return table;
}

const std::vector<std::string>& PoeNinjaCategories()
{
    static const std::vector<std::string> categories = { "Runes",     "Currency", "UncutGems",          "Expedition",
                                                         "Ritual",    "Breach",   "Verisium",           "Idols",
                                                         "SoulCores", "Essences", "LineageSupportGems", "Abyss",
                                                         "Fragments" };

    return categories;
}
}

PriceTable PoeNinjaPriceProvider::DownloadPrices(const std::string& league, const std::stop_token& stop)
{
    LOG_INFO("PoeNinjaPriceProvider::DownloadPrices() -> " + PriceApiBase());

    ProxyFailures().store(0);

    PriceTable result;
    result.items.reserve(512);

    const std::string encodedLeague = EncodeUrlComponent(league);

    cpr::Session session;
    ConfigureSession(session, stop);

    std::vector<std::string> failed;

    for (const auto& category : PoeNinjaCategories())
    {
        if (stop.stop_requested())
        {
            result.items.clear();
            result.complete = false;
            return result;
        }

        auto dump = DownloadCategory(session, encodedLeague, category, stop);

        LOG_INFO("Downloaded " + category + ": " + std::to_string(dump.items.size()));

        if (!dump.complete)
        {
            failed.push_back(category);
            result.complete = false;
        }

        for (auto& [name, info] : dump.items)
            result.items[name] = info;

        if (result.divineToEx <= 0.0)
            result.divineToEx = dump.divineToEx;
    }

    if (!failed.empty())
    {
        std::string names;

        for (const auto& category : failed)
        {
            if (!names.empty())
                names += ", ";

            names += category;
        }

        LOG_ERROR(
            "PoeNinjaPriceProvider::DownloadPrices() -> " + std::to_string(failed.size()) + " of " +
            std::to_string(PoeNinjaCategories().size()) + " categories failed: " + names
        );
    }

    LOG_INFO("PoeNinjaPriceProvider::DownloadPrices() -> total prices: " + std::to_string(result.items.size()));

    return result;
}

std::string PoeNinjaPriceProvider::EncodeUrlComponent(const std::string& text)
{
    std::ostringstream out;
    out << std::uppercase << std::hex;

    for (unsigned char ch : text)
    {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
            ch == '~')
        {
            out << static_cast<char>(ch);
        }
        else if (ch == ' ')
        {
            out << '+';
        }
        else
        {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }

    return out.str();
}

PriceTable PoeNinjaPriceProvider::DownloadCategory(
    cpr::Session& session,
    const std::string& encodedLeague,
    const std::string& type,
    const std::stop_token& stop
)
{
    const std::string query = "?league=" + encodedLeague + "&type=" + type;

    auto parse = [](const std::string& text) -> PriceTable
    {
        json j = json::parse(text, nullptr, false);

        if (j.is_discarded())
        {
            LOG_ERROR("PoeNinjaPriceProvider JSON parse failed");
            return FailedTable();
        }

        return ParseCategoryDump(j);
    };

    std::string body;

    if (ProxyFailures().load() < kProxyFailureLimit)
    {
        if (Fetch(session, PriceApiBase() + query, body, stop))
        {
            ProxyFailures().store(0);
            return parse(body);
        }

        if (stop.stop_requested())
            return FailedTable();

        const int failures = ProxyFailures().fetch_add(1) + 1;

        LOG_ERROR(
            "Price proxy unavailable (" + std::to_string(failures) + "/" + std::to_string(kProxyFailureLimit) +
            "), falling back to poe.ninja directly"
        );
    }

    if (!Fetch(session, kDirectApi + query, body, stop))
        return FailedTable();

    return parse(body);
}

PriceTable PoeNinjaPriceProvider::ParseCategoryDump(const json& j)
{
    PriceTable result;

    const auto core = j.find("core");
    const auto items = j.find("items");
    const auto lines = j.find("lines");

    if (core == j.end() || !core->is_object() || items == j.end() || !items->is_array() || lines == j.end() || !lines->is_array())
    {
        LOG_ERROR("PoeNinjaPriceProvider::ParseCategoryDump() invalid JSON structure");
        result.complete = false;
        return result;
    }

    const auto rates = core->find("rates");
    const double divineToEx = rates == core->end() ? 0.0 : JsonValue(*rates, "exalted", 0.0);

    if (divineToEx <= 0.0)
    {
        LOG_ERROR("PoeNinjaPriceProvider::ParseCategoryDump() invalid exalted rate");
        result.complete = false;
        return result;
    }

    result.divineToEx = divineToEx;

    std::unordered_map<std::string, std::string> idToName;
    idToName.reserve(items->size());

    for (const auto& item : *items)
    {
        std::string id = JsonValue(item, "id", "");
        std::string name = JsonValue(item, "name", "");

        if (!id.empty() && !name.empty())
            idToName.emplace(std::move(id), std::move(name));
    }

    result.items.reserve(lines->size());
    for (const auto& line : *lines)
    {
        const std::string id = JsonValue(line, "id", "");

        if (id.empty())
            continue;

        auto it = idToName.find(id);

        if (it == idToName.end())
            continue;

        const double primaryValue = JsonValue(line, "primaryValue", 0.0);

        if (primaryValue <= 0.0)
            continue;

        result.items[it->second] = PriceInfo{ primaryValue * divineToEx };
    }

    LOG_INFO("PoeNinjaPriceProvider::ParseCategoryDump() parsed prices: " + std::to_string(result.items.size()));

    return result;
}
