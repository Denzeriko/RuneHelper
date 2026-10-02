#include "platform/CursorPosition.h"

#include <windows.h>

std::optional<CursorPosition> QueryCursorPosition()
{
    POINT point{};

    if (!GetCursorPos(&point))
        return std::nullopt;

    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);

    if (!monitor || !GetMonitorInfoW(monitor, &info))
        return std::nullopt;

    return CursorPosition{ point.x,
                           point.y,
                           info.rcMonitor.right - info.rcMonitor.left,
                           info.rcMonitor.bottom - info.rcMonitor.top,
                           info.rcMonitor.left,
                           info.rcMonitor.top };
}
