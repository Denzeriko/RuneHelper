#include "core/AtomicFile.h"

#include <fstream>
#include <system_error>

bool WriteFileAtomic(const std::filesystem::path& path, std::string_view content)
{
    if (path.empty())
        return false;

    std::filesystem::path temp = path;
    temp += ".tmp";

    std::error_code ec;
    std::filesystem::remove(temp, ec);

    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);

        if (!file)
            return false;

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();

        if (!file)
        {
            file.close();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

    std::filesystem::rename(temp, path, ec);

    if (ec)
    {
        std::filesystem::remove(temp, ec);
        return false;
    }

    return true;
}
