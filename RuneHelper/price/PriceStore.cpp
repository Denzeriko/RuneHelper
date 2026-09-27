#include "price/PriceStore.h"

#include <filesystem>
#include <fstream>

#include "common/AtomicFile.h"
#include "common/JsonRead.h"
#include "common/Logger.h"
#include "platform/PlatformPaths.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace
{
std::string DumpFileNameForLeague(const std::string& league)
{
    std::string suffix;
    suffix.reserve(league.size());

    for (unsigned char ch : league)
    {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            suffix.push_back(static_cast<char>(ch));
        else if (suffix.empty() || suffix.back() != '_')
            suffix.push_back('_');
    }

    while (!suffix.empty() && suffix.back() == '_')
        suffix.pop_back();

    if (suffix.empty())
        suffix = "unknown";

    return "prices_dump_" + suffix + ".json";
}

std::filesystem::path DumpPathForLeague(const std::string& league)
{
    return GetUserDataDir() / DumpFileNameForLeague(league);
}
}

std::optional<PriceDump> JsonPriceStore::Load(const std::string& league)
{
    std::ifstream file(DumpPathForLeague(league));

    if (!file)
        return std::nullopt;

    json value = json::parse(file, nullptr, false);

    if (value.is_discarded())
    {
        LOG_ERROR("PriceCache::LoadDump() -> JSON parse failed");
        return std::nullopt;
    }

    if (!value.contains("items") || !value["items"].is_object())
        return std::nullopt;

    PriceDump dump;

    for (auto it = value["items"].begin(); it != value["items"].end(); ++it)
    {
        if (it.value().is_number())
            dump.items[it.key()] = PriceInfo{ it.value().get<double>() };
    }

    dump.divineToEx = JsonValue(value, "divine_to_ex", 0.0);
    dump.updatedAt = JsonValue<std::int64_t>(value, "dump_updated_at", 0);
    return dump;
}

bool JsonPriceStore::Save(const std::string& league, const PriceDump& dump)
{
    json value;
    value["league"] = league;
    value["dump_updated_at"] = dump.updatedAt;
    value["divine_to_ex"] = dump.divineToEx;
    value["items"] = json::object();

    for (const auto& [name, info] : dump.items)
        value["items"][name] = info.ex;

    if (!WriteFileAtomic(DumpPathForLeague(league), value.dump(4)))
        return false;

    return true;
}

std::unique_ptr<PriceStore> CreatePriceStore()
{
    return std::make_unique<JsonPriceStore>();
}
