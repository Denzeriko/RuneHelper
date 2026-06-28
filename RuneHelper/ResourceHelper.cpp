#include "ResourceHelper.h"
#include "resource.h"

#include <windows.h>
#include <fstream>

#include "Logger.h"

bool ExtractResourceToFile(int resId, const wchar_t* resType, const std::filesystem::path& outPath)
{
    LOG_INFO("ExtractResourceToFile() -> call");

    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), resType);
    if (!hRes) return false;

    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return false;

    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hData);
    if (!data || size == 0) return false;

    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream file(outPath, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data), size);

    LOG_INFO("ExtractResourceToFile() -> return true");

    return file.good();
}

std::string PrepareTessdata()
{
    LOG_INFO("PrepareTessdata() -> call");

    auto dir = std::filesystem::temp_directory_path() / "RuneHelper" / "tessdata";
    auto eng = dir / "eng.traineddata";

    LOG_INFO("PrepareTessdata() -> path: " + dir.string());
    LOG_INFO("PrepareTessdata() -> eng path: " + eng.string());
    LOG_INFO("PrepareTessdata() -> extracting resource...");

    if (!std::filesystem::exists(eng))
    {
        ExtractResourceToFile(IDR_ENG_TRAINEDDATA, RT_RCDATA, eng);
    }

    LOG_INFO("PrepareTessdata() -> return " + dir.string());

    return dir.string();
}