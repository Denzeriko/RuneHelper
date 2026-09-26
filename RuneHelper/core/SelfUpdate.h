#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

struct UpdatePaths
{
    std::filesystem::path current;
    std::filesystem::path download;
    std::filesystem::path backup;
};

UpdatePaths UpdatePathsFor(const std::filesystem::path& executable);
bool CanReplace(const UpdatePaths& paths);
bool FileMatches(const std::filesystem::path& file, std::uintmax_t size, std::string_view sha256);
bool MakeExecutable(const std::filesystem::path& file);
bool SwapExecutable(const UpdatePaths& paths);
void RemoveUpdateLeftovers(const UpdatePaths& paths);
