#include "platform/CursorPosition.h"

#include "WaylandSession.h"

#ifdef RUNEHELPER_WAYLAND_X11_CURSOR
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
struct XOutputGeometry
{
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct XDisplayHandle
{
    Display* value = XOpenDisplay(nullptr);
    int eventBase = 0;
    bool randrAvailable = false;
    bool outputsLoaded = false;
    std::vector<XOutputGeometry> outputs;

    XDisplayHandle()
    {
        if (!value)
            return;

        int errorBase = 0;
        int major = 1;
        int minor = 3;
        randrAvailable = XRRQueryExtension(value, &eventBase, &errorBase) && XRRQueryVersion(value, &major, &minor) &&
                         (major > 1 || (major == 1 && minor >= 3));
        if (randrAvailable)
            XRRSelectInput(
                value,
                DefaultRootWindow(value),
                RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask
            );
    }

    ~XDisplayHandle()
    {
        if (value)
            XCloseDisplay(value);
    }

    void RefreshOutputs()
    {
        bool changed = !outputsLoaded;
        while (XPending(value))
        {
            XEvent event{};
            XNextEvent(value, &event);
            if (event.type == eventBase + RRScreenChangeNotify || event.type == eventBase + RRNotify)
                changed = true;
        }
        if (!changed)
            return;

        outputsLoaded = false;
        outputs.clear();
        XRRScreenResources* resources = XRRGetScreenResourcesCurrent(value, DefaultRootWindow(value));
        if (!resources)
            return;

        for (int i = 0; i < resources->noutput; ++i)
        {
            XRROutputInfo* output = XRRGetOutputInfo(value, resources, resources->outputs[i]);
            if (!output)
                continue;

            if (output->connection == RR_Connected && output->crtc)
            {
                if (XRRCrtcInfo* crtc = XRRGetCrtcInfo(value, resources, output->crtc))
                {
                    if (crtc->width > 0 && crtc->height > 0)
                        outputs.push_back({ std::string(output->name, static_cast<std::size_t>(output->nameLen)),
                                            crtc->x,
                                            crtc->y,
                                            static_cast<int>(crtc->width),
                                            static_cast<int>(crtc->height) });
                    XRRFreeCrtcInfo(crtc);
                }
            }
            XRRFreeOutputInfo(output);
        }
        XRRFreeScreenResources(resources);
        outputsLoaded = true;
    }
};

int MapCoordinate(int coordinate, int sourceOrigin, int sourceExtent, int targetOrigin, int targetExtent)
{
    const auto offset = static_cast<std::int64_t>(coordinate - sourceOrigin) * targetExtent / sourceExtent;
    return targetOrigin + std::clamp(static_cast<int>(offset), 0, targetExtent - 1);
}
}
#endif

std::optional<CursorPosition> QueryCursorPosition()
{
#ifdef RUNEHELPER_WAYLAND_X11_CURSOR
    static XDisplayHandle connection;
    Display* display = connection.value;

    if (!display || !connection.randrAvailable)
        return std::nullopt;

    Window returnedRoot = 0;
    Window returnedChild = 0;
    int rootX = 0;
    int rootY = 0;
    int windowX = 0;
    int windowY = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(display, DefaultRootWindow(display), &returnedRoot, &returnedChild, &rootX, &rootY, &windowX, &windowY, &mask))
        return std::nullopt;

    static WaylandSession session;
    if (!session.IsConnected() && !session.Connect())
        return std::nullopt;
    if (!session.DispatchNonBlocking())
    {
        session.Disconnect();
        return std::nullopt;
    }

    connection.RefreshOutputs();
    for (const auto& source : connection.outputs)
    {
        if (rootX < source.x || rootX >= source.x + source.width || rootY < source.y || rootY >= source.y + source.height)
            continue;

        for (const WaylandOutput& target : session.Outputs())
        {
            if (target.name != source.name || target.LogicalWidth() <= 0 || target.LogicalHeight() <= 0)
                continue;

            CursorPosition position;
            position.screenX = target.x;
            position.screenY = target.y;
            position.screenWidth = target.LogicalWidth();
            position.screenHeight = target.LogicalHeight();
            position.x = MapCoordinate(rootX, source.x, source.width, target.x, position.screenWidth);
            position.y = MapCoordinate(rootY, source.y, source.height, target.y, position.screenHeight);
            return position;
        }
    }
#endif

    return std::nullopt;
}
