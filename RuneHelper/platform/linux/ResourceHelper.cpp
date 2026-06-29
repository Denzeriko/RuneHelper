#include "ResourceHelper.h"

#include <stdexcept>

bool ExtractResourceToFile(int, const wchar_t*, const std::filesystem::path&)
{
    throw std::runtime_error("Linux resource extraction is not implemented");
}

std::string PrepareTessdata()
{
    throw std::runtime_error("Linux tessdata resource preparation is not implemented");
}
