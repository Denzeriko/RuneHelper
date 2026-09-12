#include "ResourceHelper.h"

#include <windows.h>

#include <array>
#include <fstream>

#include "core/Logger.h"
#include "platform/PlatformPaths.h"
#include "resources/resource.h"

namespace
{
struct RuneTemplateResource
{
    int id;
    const char* filename;
};

constexpr std::array<RuneTemplateResource, 8> kRuneTemplates{{
    { IDR_RUNE_TEMPLATE_1, "A1.png" },
    { IDR_RUNE_TEMPLATE_2, "A2.png" },
    { IDR_RUNE_TEMPLATE_3, "B1.png" },
    { IDR_RUNE_TEMPLATE_4, "C1.png" },
    { IDR_RUNE_TEMPLATE_5, "C2.png" },
    { IDR_RUNE_TEMPLATE_6, "C3.png" },
    { IDR_RUNE_TEMPLATE_7, "S1.png" },
    { IDR_RUNE_TEMPLATE_8, "S2.png" },
}};
}

bool ExtractResourceToFile(int resId, LPCWSTR resType, const std::filesystem::path& outPath)
{
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), resType);
    if (!hRes)
    {
        LOG_ERROR("FindResourceW failed: " + std::to_string(GetLastError()));
        return false;
    }

    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData)
    {
        LOG_ERROR("LoadResource failed");
        return false;
    }

    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hData);

    if (!data || size == 0)
    {
        LOG_ERROR("LockResource/SizeofResource failed");
        return false;
    }

    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream file(outPath, std::ios::binary);
    if (!file)
    {
        LOG_ERROR("Failed to create output file: " + outPath.string());
        return false;
    }

    file.write(reinterpret_cast<const char*>(data), size);

    return file.good();
}

std::string PrepareTessdata()
{
    LOG_INFO("PrepareTessdata() -> call");

    auto dir = GetUserDataDir() / "tessdata";
    auto eng = dir / "eng.traineddata";

    LOG_INFO("PrepareTessdata() -> path: " + dir.string());

    if (!std::filesystem::exists(eng))
    {
        LOG_INFO("PrepareTessdata() -> extracting eng.traineddata");

        if (!ExtractResourceToFile(IDR_ENG_TRAINEDDATA, MAKEINTRESOURCEW(10), eng))
        {
            LOG_ERROR("PrepareTessdata() -> failed to extract eng.traineddata");
            return {};
        }
    }
    else
    {
        LOG_INFO("PrepareTessdata() -> eng.traineddata already exists");
    }

    return dir.string();
}

std::filesystem::path PrepareRuneTemplates()
{
    LOG_INFO("PrepareRuneTemplates() -> call");

    const auto dir = GetUserDataDir() / "runes";
    std::filesystem::create_directories(dir);

    for (const auto& runeTemplate : kRuneTemplates)
    {
        const auto outPath = dir / runeTemplate.filename;

        if (std::filesystem::exists(outPath))
            continue;

        if (!ExtractResourceToFile(runeTemplate.id, MAKEINTRESOURCEW(10), outPath))
            LOG_ERROR("PrepareRuneTemplates() -> failed to extract " + outPath.string());
    }

    return dir;
}
