#include "ResourceHelper.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "core/AtomicFile.h"
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

    return WriteFileAtomic(
        destination,
        std::string_view(
            reinterpret_cast<const char*>(resource.begin),
            static_cast<std::size_t>(resource.end - resource.begin)
        )
    );
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

    const auto dir = GetUserDataDir() / "tessdata";
    auto eng = dir / "eng.traineddata";

    if (!std::filesystem::exists(eng))
        ExtractResourceToFile(0, nullptr, eng);

    LOG_INFO("Linux PrepareTessdata() -> return " + dir.string());

    return dir.string();
}

std::string LoadEmbeddedRecipeDatabase()
{
    const EmbeddedResource* resource = FindEmbedded("combinations.json");

    if (!resource)
    {
        LOG_ERROR("Linux recipe database is not embedded in the binary");
        return {};
    }

    return std::string(
        reinterpret_cast<const char*>(resource->begin),
        static_cast<std::size_t>(resource->end - resource->begin)
    );
}
