#include "ResourceHelper.h"

#include <windows.h>

#include <string>
#include <string_view>

#include "core/AtomicFile.h"
#include "core/Logger.h"
#include "platform/PlatformPaths.h"
#include "resources/resource.h"

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

    if (!WriteFileAtomic(outPath, std::string_view(static_cast<const char*>(data), size)))
    {
        LOG_ERROR("Failed to write output file: " + outPath.string());
        return false;
    }

    return true;
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

std::string LoadEmbeddedRecipeDatabase()
{
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_COMBINATIONS), MAKEINTRESOURCEW(10));

    if (!hRes)
    {
        LOG_ERROR("Recipe database resource not found: " + std::to_string(GetLastError()));
        return {};
    }

    HGLOBAL hData = LoadResource(nullptr, hRes);
    const DWORD size = SizeofResource(nullptr, hRes);
    const void* data = hData ? LockResource(hData) : nullptr;

    if (!data || size == 0)
    {
        LOG_ERROR("Recipe database resource could not be locked");
        return {};
    }

    return std::string(static_cast<const char*>(data), size);
}
