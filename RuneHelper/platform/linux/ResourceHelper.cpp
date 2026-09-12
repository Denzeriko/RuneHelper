#include "ResourceHelper.h"

#include <fstream>
#include <string>
#include <string_view>

#include "core/Logger.h"
#include "platform/PlatformPaths.h"
#include "platform/linux/EmbeddedResources.h"

namespace
{
const EmbeddedResource* FindEmbedded(std::string_view name)
{
    for (const EmbeddedResource& resource : GetEmbeddedResources())
    {
        if (resource.name == name)
            return &resource;
    }

    return nullptr;
}

bool WriteEmbedded(const EmbeddedResource& resource, const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination.parent_path());

    std::ofstream out(destination, std::ios::binary);

    if (!out)
        return false;

    out.write(
        reinterpret_cast<const char*>(resource.begin),
        static_cast<std::streamsize>(resource.end - resource.begin)
    );

    return out.good();
}
}

bool ExtractResourceToFile(int, const wchar_t*, const std::filesystem::path& outPath)
{
    const EmbeddedResource* resource = FindEmbedded("eng.traineddata_fast");

    if (!resource)
    {
        LOG_ERROR("Linux resource extraction failed: eng.traineddata_fast is not embedded in the binary");
        return false;
    }

    if (!WriteEmbedded(*resource, outPath))
    {
        LOG_ERROR("Linux resource extraction failed: could not write " + outPath.string());
        return false;
    }

    LOG_INFO("Linux tessdata written from the embedded resource: " + outPath.string());
    return true;
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

std::filesystem::path PrepareRuneTemplates()
{
    LOG_INFO("Linux PrepareRuneTemplates() -> call");

    const auto dir = GetUserDataDir() / "runes";
    std::filesystem::create_directories(dir);

    for (const EmbeddedResource& resource : GetEmbeddedResources())
    {
        if (!resource.name.ends_with(".png"))
            continue;

        const std::filesystem::path destination = dir / std::string(resource.name);

        if (std::filesystem::exists(destination))
            continue;

        if (!WriteEmbedded(resource, destination))
            LOG_ERROR("Linux rune template extraction failed: " + destination.string());
    }

    return dir;
}
