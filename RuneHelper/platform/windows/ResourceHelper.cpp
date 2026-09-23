#include "ResourceHelper.h"

#include <windows.h>

#include <string>
#include <string_view>

#include "core/Logger.h"
#include "resources/resource.h"

namespace
{
std::string_view ResourceBytes(int id, const char* label)
{
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));

    if (!resource)
    {
        LOG_ERROR(std::string(label) + " resource not found: " + std::to_string(GetLastError()));
        return {};
    }

    HGLOBAL handle = LoadResource(nullptr, resource);
    const DWORD size = SizeofResource(nullptr, resource);
    const void* data = handle ? LockResource(handle) : nullptr;

    if (!data || size == 0)
    {
        LOG_ERROR(std::string(label) + " resource could not be locked");
        return {};
    }

    return std::string_view(static_cast<const char*>(data), size);
}
}

std::string_view EmbeddedTraineddata()
{
    return ResourceBytes(IDR_ENG_TRAINEDDATA, "Tessdata");
}

std::string LoadEmbeddedRecipeDatabase()
{
    return std::string(ResourceBytes(IDR_COMBINATIONS, "Recipe database"));
}
