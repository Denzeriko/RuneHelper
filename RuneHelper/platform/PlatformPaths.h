#pragma once

#include <filesystem>
#include <string_view>

#ifdef _WIN32
inline constexpr std::string_view NULL_DEVICE = "NUL";
#else
inline constexpr std::string_view NULL_DEVICE = "/dev/null";
#endif

std::filesystem::path GetAppDataDir();
