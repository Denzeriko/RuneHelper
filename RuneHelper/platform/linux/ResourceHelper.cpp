#include "ResourceHelper.h"

#include "core/Logger.h"

bool ExtractResourceToFile(int, const wchar_t*, const std::filesystem::path&)
{
    LOG_ERROR("Linux resource extraction is not implemented");
    return false;
}

std::string PrepareTessdata()
{
    LOG_ERROR("Linux tessdata resource preparation is not implemented; using resources directory");
    return "resources";
}
