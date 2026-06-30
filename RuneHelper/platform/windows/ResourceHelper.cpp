#include "ResourceHelper.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fstream>

#include "core/Logger.h"
#include "core/Helpers.h"
#include "resources/resource.h"

bool ExtractResourceToFile(int resId, const wchar_t* resType, const std::filesystem::path& outPath)
{
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), resType);
    if (!hRes)
    {
        LOG_ERROR("FindResourceW failed");
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

    auto dir = GetAppDataDir() / "tessdata";
    auto eng = dir / "eng.traineddata";

    LOG_INFO("PrepareTessdata() -> path: " + dir.string());

    if (!std::filesystem::exists(eng))
    {
        LOG_INFO("PrepareTessdata() -> extracting eng.traineddata");

        if (!ExtractResourceToFile(IDR_ENG_TRAINEDDATA, RT_RCDATA, eng))
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