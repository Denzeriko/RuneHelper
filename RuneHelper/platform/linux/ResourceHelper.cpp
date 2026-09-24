#include "ResourceHelper.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "core/Logger.h"
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

std::string_view Bytes(const EmbeddedResource& resource)
{
    return std::string_view(reinterpret_cast<const char*>(resource.begin), static_cast<std::size_t>(resource.end - resource.begin));
}
}

std::string_view EmbeddedTextModel()
{
    const EmbeddedResource* resource = FindEmbedded("text_model.bin");

    if (!resource)
    {
        LOG_ERROR("Linux text model is not embedded in the binary");
        return {};
    }

    return Bytes(*resource);
}

std::string LoadEmbeddedRecipeDatabase()
{
    const EmbeddedResource* resource = FindEmbedded("combinations.json");

    if (!resource)
    {
        LOG_ERROR("Linux recipe database is not embedded in the binary");
        return {};
    }

    return std::string(Bytes(*resource));
}
