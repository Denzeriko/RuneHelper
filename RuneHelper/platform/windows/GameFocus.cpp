#include "platform/GameFocus.h"

#include <windows.h>

#include <iterator>
#include <string_view>

namespace
{
constexpr std::wstring_view kGameWindowClass = L"POEWindowClass";
constexpr const wchar_t* kGameWindowTitle = L"Path of Exile 2";
}

bool GameFocusSupported()
{
    return true;
}

GameFocus QueryGameFocus()
{
    const HWND window = GetForegroundWindow();

    if (!window)
        return GameFocus::Unknown;

    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);

    if (process == GetCurrentProcessId())
        return GameFocus::Active;

    wchar_t className[64] = {};
    const int classLength = GetClassNameW(window, className, static_cast<int>(std::size(className)));

    if (std::wstring_view(className, classLength > 0 ? classLength : 0) == kGameWindowClass)
        return GameFocus::Active;

    if (window == FindWindowW(nullptr, kGameWindowTitle))
        return GameFocus::Active;

    return GameFocus::Inactive;
}
