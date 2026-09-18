#include "ui/OverlayText.h"

#ifdef _WIN32
#include <windows.h>
#endif

std::wstring OverlayWide(const std::string& text)
{
    if (text.empty())
        return L"";

#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

    if (size <= 0)
        return L"";

    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);

    return result;
#else
    return std::wstring(text.begin(), text.end());
#endif
}
