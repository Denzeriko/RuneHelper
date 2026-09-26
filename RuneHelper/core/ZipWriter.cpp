#include "core/ZipWriter.h"

#include <algorithm>
#include <array>

namespace
{
constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::uint32_t kZipVersion = 20;
constexpr std::uint32_t kUtf8NamesFlag = 0x0800;
constexpr std::uint32_t kStoredMethod = 0;
constexpr int kDosEpochYear = 80;

const std::array<std::uint32_t, 256>& CrcTable()
{
    static const std::array<std::uint32_t, 256> table = []
    {
        std::array<std::uint32_t, 256> values{};

        for (std::uint32_t i = 0; i < values.size(); ++i)
        {
            std::uint32_t value = i;

            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1u) ? (value >> 1) ^ 0xEDB88320u : value >> 1;

            values[i] = value;
        }

        return values;
    }();

    return table;
}

void Put16(std::string& out, std::uint32_t value)
{
    out += static_cast<char>(value & 0xffu);
    out += static_cast<char>((value >> 8) & 0xffu);
}

void Put32(std::string& out, std::uint32_t value)
{
    Put16(out, value & 0xffffu);
    Put16(out, value >> 16);
}

std::uint32_t DosTime(const std::tm& time)
{
    return static_cast<std::uint32_t>((time.tm_hour << 11) | (time.tm_min << 5) | (time.tm_sec / 2));
}

std::uint32_t DosDate(const std::tm& time)
{
    const int year = std::max(time.tm_year - kDosEpochYear, 0);
    return static_cast<std::uint32_t>((year << 9) | ((time.tm_mon + 1) << 5) | time.tm_mday);
}

void PutEntryFields(std::string& out, const ZipEntry& entry, std::uint32_t crc, const std::tm& modified)
{
    const auto size = static_cast<std::uint32_t>(entry.data.size());

    Put16(out, kZipVersion);
    Put16(out, kUtf8NamesFlag);
    Put16(out, kStoredMethod);
    Put16(out, DosTime(modified));
    Put16(out, DosDate(modified));
    Put32(out, crc);
    Put32(out, size);
    Put32(out, size);
    Put16(out, static_cast<std::uint32_t>(entry.name.size()));
    Put16(out, 0);
}
}

std::uint32_t Crc32(std::string_view data)
{
    const std::array<std::uint32_t, 256>& table = CrcTable();
    std::uint32_t crc = 0xFFFFFFFFu;

    for (const char c : data)
        crc = table[(crc ^ static_cast<unsigned char>(c)) & 0xffu] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFFu;
}

std::string BuildZip(const std::vector<ZipEntry>& entries, const std::tm& modified)
{
    std::string archive;
    std::string directory;

    for (const ZipEntry& entry : entries)
    {
        const std::uint32_t crc = Crc32(entry.data);
        const auto offset = static_cast<std::uint32_t>(archive.size());

        Put32(archive, kLocalHeaderSignature);
        PutEntryFields(archive, entry, crc, modified);
        archive += entry.name;
        archive += entry.data;

        Put32(directory, kCentralHeaderSignature);
        Put16(directory, kZipVersion);
        PutEntryFields(directory, entry, crc, modified);
        Put16(directory, 0);
        Put16(directory, 0);
        Put16(directory, 0);
        Put32(directory, 0);
        Put32(directory, offset);
        directory += entry.name;
    }

    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    const auto count = static_cast<std::uint32_t>(entries.size());

    archive += directory;

    Put32(archive, kEndSignature);
    Put16(archive, 0);
    Put16(archive, 0);
    Put16(archive, count);
    Put16(archive, count);
    Put32(archive, static_cast<std::uint32_t>(directory.size()));
    Put32(archive, directoryOffset);
    Put16(archive, 0);

    return archive;
}
