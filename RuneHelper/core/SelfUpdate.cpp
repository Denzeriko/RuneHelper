#include "core/SelfUpdate.h"

#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "core/Logger.h"
#include "core/Sha256.h"
#include "platform/PlatformPaths.h"

namespace
{
constexpr std::size_t kHashChunk = 1 << 20;

std::filesystem::path Sibling(const std::filesystem::path& executable, std::string_view tag)
{
    std::filesystem::path name = executable.stem();
    name += std::string(tag);
    name += executable.extension();

    return executable.parent_path() / name;
}
}

UpdatePaths UpdatePathsFor(const std::filesystem::path& executable)
{
    return { executable, Sibling(executable, ".new"), Sibling(executable, ".old") };
}

bool CanReplace(const UpdatePaths& paths)
{
    {
        std::ofstream probe(paths.download, std::ios::binary | std::ios::trunc);

        if (!probe)
            return false;
    }

    std::error_code ec;
    std::filesystem::remove(paths.download, ec);

    return true;
}

bool FileMatches(const std::filesystem::path& file, std::uintmax_t size, std::string_view sha256)
{
    std::error_code ec;

    if (std::filesystem::file_size(file, ec) != size || ec)
        return false;

    std::ifstream in(file, std::ios::binary);
    std::vector<char> chunk(kHashChunk);
    Sha256 hash;

    while (in.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || in.gcount() > 0)
        hash.Update(std::string_view(chunk.data(), static_cast<std::size_t>(in.gcount())));

    return !in.bad() && hash.FinishHex() == sha256;
}

bool MakeExecutable(const std::filesystem::path& file)
{
    using std::filesystem::perms;

    std::error_code ec;
    std::filesystem::permissions(
        file,
        perms::owner_all | perms::group_read | perms::group_exec | perms::others_read | perms::others_exec,
        std::filesystem::perm_options::replace,
        ec
    );

    return !ec;
}

bool SwapExecutable(const UpdatePaths& paths)
{
    std::error_code ec;
    std::filesystem::remove(paths.backup, ec);
    std::filesystem::rename(paths.current, paths.backup, ec);

    if (ec)
    {
        LOG_ERROR("Update: the running binary could not be moved aside: " + ec.message());
        return false;
    }

    std::filesystem::rename(paths.download, paths.current, ec);

    if (!ec)
        return true;

    LOG_ERROR("Update: the new binary could not be put in place: " + ec.message());

    std::error_code restore;
    std::filesystem::rename(paths.backup, paths.current, restore);

    if (restore)
        LOG_ERROR("Update: the previous binary could not be restored from " + PathToUtf8(paths.backup) + ": " + restore.message());

    return false;
}

void RemoveUpdateLeftovers(const UpdatePaths& paths)
{
    for (const std::filesystem::path& leftover : { paths.download, paths.backup })
    {
        std::error_code ec;

        if (std::filesystem::remove(leftover, ec))
            LOG_INFO("Update: removed " + PathToUtf8(leftover));
    }
}
