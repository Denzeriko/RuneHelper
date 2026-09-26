#include "core/BugReport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <vector>

#include "core/AtomicFile.h"
#include "core/Logger.h"
#include "core/ZipWriter.h"
#include "platform/PlatformPaths.h"

namespace
{
constexpr std::uintmax_t kMaxLogBytes = 1024ULL * 1024;
constexpr std::uintmax_t kWholeFile = std::numeric_limits<std::uintmax_t>::max();
constexpr const char* kLogNames[] = { "runehelper.log", "runehelper.old.log" };

std::tm LocalTimeNow()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::tm time{};
#ifdef _WIN32
    localtime_s(&time, &now);
#else
    localtime_r(&now, &time);
#endif

    return time;
}

std::string ZipName(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

bool ReadTail(const std::filesystem::path& path, std::uintmax_t maxBytes, std::string& data)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);

    if (ec)
        return false;

    std::ifstream in(path, std::ios::binary);

    if (!in)
        return false;

    const std::uintmax_t skip = size > maxBytes ? size - maxBytes : 0;
    in.seekg(static_cast<std::streamoff>(skip));
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    const std::size_t lineEnd = skip > 0 ? data.find('\n') : std::string::npos;

    if (lineEnd != std::string::npos)
        data.erase(0, lineEnd + 1);

    return true;
}

void AddFile(std::vector<ZipEntry>& entries, const std::filesystem::path& path, std::string name, std::uintmax_t maxBytes)
{
    ZipEntry entry{ std::move(name), {} };

    if (ReadTail(path, maxBytes, entry.data))
        entries.push_back(std::move(entry));
}

void AddFolder(std::vector<ZipEntry>& entries, const std::filesystem::path& root, const std::filesystem::path& folder)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;

    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
    {
        if (it->is_regular_file(ec))
            files.push_back(it->path());
    }

    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files)
        AddFile(entries, file, ZipName(file.lexically_relative(root)), kWholeFile);
}

std::string ReportFileName(const std::tm& time)
{
    char buffer[64];
    const std::size_t length = std::strftime(buffer, sizeof(buffer), "runehelper-report-%Y%m%d-%H%M%S.zip", &time);

    return std::string(buffer, length);
}
}

std::optional<std::filesystem::path> WriteBugReport(const std::string& systemInfo)
{
    const std::filesystem::path& dataDir = GetUserDataDir();
    const std::tm now = LocalTimeNow();

    std::vector<ZipEntry> entries;
    entries.push_back({ "system.txt", systemInfo });

    AddFile(entries, dataDir / "config.json", "config.json", kWholeFile);

    for (const char* log : kLogNames)
        AddFile(entries, dataDir / log, log, kMaxLogBytes);

    AddFolder(entries, dataDir, dataDir / "ocr_debug" / "latest");

    const std::filesystem::path folder = dataDir / "reports";
    const std::filesystem::path report = folder / ReportFileName(now);

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    if (ec || !WriteFileAtomic(report, BuildZip(entries, now)))
    {
        LOG_ERROR("Bug report could not be written to " + PathToUtf8(report));
        return std::nullopt;
    }

    LOG_INFO("Bug report written to " + PathToUtf8(report) + " with " + std::to_string(entries.size()) + " files");
    return report;
}
