#pragma once

#include <filesystem>
#include <string_view>

bool WriteFileAtomic(const std::filesystem::path& path, std::string_view content);
