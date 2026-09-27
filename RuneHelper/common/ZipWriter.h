#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

struct ZipEntry
{
    std::string name;
    std::string data;
};

std::uint32_t Crc32(std::string_view data);
std::string BuildZip(const std::vector<ZipEntry>& entries, const std::tm& modified);
