#pragma once

#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

template <typename T>
T JsonValue(const nlohmann::json& object, const char* key, T fallback)
{
    const auto it = object.find(key);

    if (it == object.end())
        return fallback;

    if constexpr (std::is_same_v<T, bool>)
        return it->is_boolean() ? it->get<bool>() : fallback;
    else if constexpr (std::is_integral_v<T>)
        return it->is_number_integer() ? it->get<T>() : fallback;
    else if constexpr (std::is_floating_point_v<T>)
        return it->is_number() ? it->get<T>() : fallback;
    else
        return it->is_string() ? it->get<T>() : fallback;
}

inline std::string JsonValue(const nlohmann::json& object, const char* key, const char* fallback)
{
    return JsonValue<std::string>(object, key, fallback);
}
