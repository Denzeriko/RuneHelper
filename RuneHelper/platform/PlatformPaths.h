#pragma once

#include <filesystem>
#include <string>

const std::filesystem::path& GetUserDataDir();

inline std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}
