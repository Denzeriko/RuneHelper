#include "platform/CursorPosition.h"

#include <X11/Xlib.h>

#ifdef RUNEHELPER_X11_RANDR_CURSOR
#include <X11/extensions/Xrandr.h>
#endif

namespace
{
struct XDisplayHandle
{
    Display* value = XOpenDisplay(nullptr);

    ~XDisplayHandle()
    {
        if (value)
            XCloseDisplay(value);
    }
};
}

std::optional<CursorPosition> QueryCursorPosition()
{
    static XDisplayHandle connection;
    Display* display = connection.value;

    if (!display)
        return std::nullopt;

    const int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window returnedRoot = 0;
    Window returnedChild = 0;
    int rootX = 0;
    int rootY = 0;
    int windowX = 0;
    int windowY = 0;
    unsigned int mask = 0;
    const bool ok = XQueryPointer(display, root, &returnedRoot, &returnedChild, &rootX, &rootY, &windowX, &windowY, &mask) != 0;
    CursorPosition position;
    position.x = rootX;
    position.y = rootY;
    position.screenWidth = DisplayWidth(display, screen);
    position.screenHeight = DisplayHeight(display, screen);

#ifdef RUNEHELPER_X11_RANDR_CURSOR
    int monitorCount = 0;
    XRRMonitorInfo* monitors = XRRGetMonitors(display, root, True, &monitorCount);

    for (int index = 0; monitors && index < monitorCount; ++index)
    {
        const XRRMonitorInfo& monitor = monitors[index];

        if (rootX >= monitor.x && rootX < monitor.x + monitor.width && rootY >= monitor.y && rootY < monitor.y + monitor.height)
        {
            position.screenX = monitor.x;
            position.screenY = monitor.y;
            position.screenWidth = monitor.width;
            position.screenHeight = monitor.height;
            break;
        }
    }

    if (monitors)
        XRRFreeMonitors(monitors);
#endif

    return ok ? std::optional<CursorPosition>(position) : std::nullopt;
}
