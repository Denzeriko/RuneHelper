#include "ResourceHelper.h"

#include <windows.h>

#include <string>
#include <string_view>

#include "core/Logger.h"
#include "resources/resource.h"

namespace
{
const LPCWSTR kRcDataResourceType = MAKEINTRESOURCEW(10);

struct TextModelResource
{
    std::string_view language;
    int id = 0;
};

constexpr TextModelResource kTextModels[] = {
    { "en", IDR_TEXT_MODEL },
    { "ru", IDR_TEXT_MODEL_RU },
    { "de", IDR_TEXT_MODEL_DE },
    { "fr", IDR_TEXT_MODEL_FR },
    { "es", IDR_TEXT_MODEL_ES },
    { "pt", IDR_TEXT_MODEL_PT },
    { "ko", IDR_TEXT_MODEL_KO },
    { "ja", IDR_TEXT_MODEL_JA },
    { "th", IDR_TEXT_MODEL_TH },
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
}

std::string_view EmbeddedTextModel(std::string_view language)
{
    for (const TextModelResource& model : kTextModels)
    {
        if (model.language == language)
            return ResourceBytes(model.id, "Text model");
    }

    return {};
}

std::string LoadEmbeddedRecipeDatabase()
{
    return std::string(ResourceBytes(IDR_COMBINATIONS, "Recipe database"));
}
