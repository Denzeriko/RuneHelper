#include "platform/CursorPosition.h"

#include "WaylandSession.h"

#ifdef RUNEHELPER_WAYLAND_X11_CURSOR
#include <X11/Xlib.h>

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
#endif

std::optional<CursorPosition> QueryCursorPosition()
{
#ifdef RUNEHELPER_WAYLAND_X11_CURSOR
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
    if (ok)
    {
        static WaylandSession session;

        if ((session.IsConnected() || session.Connect()))
        {
            if (const WaylandOutput* output = session.OutputAt(rootX, rootY))
            {
                position.screenX = output->x;
                position.screenY = output->y;
                position.screenWidth = output->LogicalWidth();
                position.screenHeight = output->LogicalHeight();
            }
        }

        return position;
    }
#endif

    return std::nullopt;
}
