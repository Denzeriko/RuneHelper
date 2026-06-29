#include "ResourceHelper.h"

#include <fstream>

#include "core/Logger.h"

namespace
{
bool CopyFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination.parent_path());

    std::ifstream in(source, std::ios::binary);

    if (!in)
        return false;

    std::ofstream out(destination, std::ios::binary);

    if (!out)
        return false;

    out << in.rdbuf();
    return out.good();
}
}

bool ExtractResourceToFile(int, const wchar_t*, const std::filesystem::path& outPath)
{
    LOG_INFO("Linux resource extraction using filesystem tessdata");

#ifdef RUNEHELPER_SOURCE_DIR
    const std::filesystem::path configuredSource =
        std::filesystem::path(RUNEHELPER_SOURCE_DIR) / "resources" / "eng.traineddata_fast";

    if (CopyFile(configuredSource, outPath))
        return true;
#endif

    const std::filesystem::path source = std::filesystem::path("RuneHelper") / "resources" / "eng.traineddata_fast";

    if (CopyFile(source, outPath))
        return true;

    const std::filesystem::path fallbackSource = std::filesystem::path("resources") / "eng.traineddata_fast";

    if (CopyFile(fallbackSource, outPath))
        return true;

    LOG_ERROR("Linux resource extraction failed: could not find resources/eng.traineddata_fast");
    return false;
}

std::string PrepareTessdata()
{
    LOG_INFO("Linux PrepareTessdata() -> call");

    auto dir = std::filesystem::temp_directory_path() / "RuneHelper" / "tessdata";
    auto eng = dir / "eng.traineddata";

    if (!std::filesystem::exists(eng))
        ExtractResourceToFile(0, nullptr, eng);

    LOG_INFO("Linux PrepareTessdata() -> return " + dir.string());

    return dir.string();
}
