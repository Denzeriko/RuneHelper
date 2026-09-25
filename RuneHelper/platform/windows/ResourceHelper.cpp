#include "ResourceHelper.h"

#include <windows.h>

#include <span>
#include <string>
#include <string_view>

#include "core/Logger.h"
#include "resources/resource.h"

namespace
{
const LPCWSTR kRcDataResourceType = MAKEINTRESOURCEW(10);

struct NamedResource
{
    std::string_view name;
    int id = 0;
};

constexpr NamedResource kTextModels[] = {
    { "en", IDR_TEXT_MODEL },    { "ru", IDR_TEXT_MODEL_RU }, { "de", IDR_TEXT_MODEL_DE },
    { "fr", IDR_TEXT_MODEL_FR }, { "es", IDR_TEXT_MODEL_ES }, { "pt", IDR_TEXT_MODEL_PT },
    { "ko", IDR_TEXT_MODEL_KO }, { "ja", IDR_TEXT_MODEL_JA }, { "th", IDR_TEXT_MODEL_TH },
};

constexpr NamedResource kImages[] = {
    { "exalted_orb.png", IDR_EXALTED_ORB },
    { "divine_orb.png", IDR_DIVINE_ORB },
};

std::string_view ResourceBytes(int id, const char* label)
{
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), kRcDataResourceType);

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

std::string_view NamedResourceBytes(std::span<const NamedResource> resources, std::string_view name, const char* label)
{
    for (const NamedResource& resource : resources)
    {
        if (resource.name == name)
            return ResourceBytes(resource.id, label);
    }

    return {};
}
}

std::string_view EmbeddedTextModel(std::string_view language)
{
    return NamedResourceBytes(kTextModels, language, "Text model");
}

std::string_view EmbeddedImage(std::string_view file)
{
    return NamedResourceBytes(kImages, file, "Image");
}

std::string LoadEmbeddedRecipeDatabase()
{
    return std::string(ResourceBytes(IDR_COMBINATIONS, "Recipe database"));
}
