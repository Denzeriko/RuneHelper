#include "platform/GameFocus.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view kGameTitle = "Path of Exile 2";
constexpr std::string_view kGameClassMarkers[] = { "steam_app_2694490", "pathofexile", "gamescope" };
constexpr std::string_view kOwnClass = "runehelper";
constexpr long kMaxTitleLength = 256;

bool gSawFocusError = false;

int TrapFocusError(Display*, XErrorEvent*)
{
    gSawFocusError = true;
    return 0;
}

struct DisplayCloser
{
    void operator()(Display* display) const { XCloseDisplay(display); }
};

Display* FocusDisplay()
{
    thread_local const std::unique_ptr<Display, DisplayCloser> display(XOpenDisplay(nullptr));
    return display.get();
}

std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool ReadProperty(Display* display, Window window, Atom property, Atom type, long length, std::string& value, unsigned long& first)
{
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;

    const int status =
        XGetWindowProperty(display, window, property, 0, length, False, type, &actualType, &actualFormat, &count, &remaining, &data);

    if (status != Success || !data)
        return false;

    if (actualFormat == 8)
        value.assign(reinterpret_cast<const char*>(data), count);
    else if (actualFormat == 32 && count > 0)
        first = *reinterpret_cast<const unsigned long*>(data);

    XFree(data);
    return count > 0;
}

Window ActiveWindow(Display* display)
{
    const Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);

    if (active == None)
        return 0;

    std::string unused;
    unsigned long window = 0;

    if (!ReadProperty(display, DefaultRootWindow(display), active, XA_WINDOW, 1, unused, window))
        return 0;

    return static_cast<Window>(window);
}

bool OwnWindow(Display* display, Window window, const std::string& windowClass)
{
    if (windowClass.find(kOwnClass) != std::string::npos)
        return true;

    const Atom pidAtom = XInternAtom(display, "_NET_WM_PID", True);

    if (pidAtom == None)
        return false;

    std::string unused;
    unsigned long pid = 0;

    return ReadProperty(display, window, pidAtom, XA_CARDINAL, 1, unused, pid) && pid == static_cast<unsigned long>(getpid());
}

std::string WindowClass(Display* display, Window window)
{
    XClassHint hint{};

    if (!XGetClassHint(display, window, &hint))
        return {};

    std::string result;

    if (hint.res_name)
        result += hint.res_name;

    result += ' ';

    if (hint.res_class)
        result += hint.res_class;

    XFree(hint.res_name);
    XFree(hint.res_class);

    return Lowercase(result);
}

std::string WindowTitle(Display* display, Window window)
{
    std::string title;
    unsigned long unused = 0;
    const Atom name = XInternAtom(display, "_NET_WM_NAME", True);
    const Atom utf8 = XInternAtom(display, "UTF8_STRING", True);

    if (name != None && utf8 != None && ReadProperty(display, window, name, utf8, kMaxTitleLength, title, unused))
        return title;

    ReadProperty(display, window, XA_WM_NAME, XA_STRING, kMaxTitleLength, title, unused);
    return title;
}

GameFocus Classify(Display* display, Window window)
{
    if (window == 0)
        return GameFocus::Unknown;

    const std::string windowClass = WindowClass(display, window);

    if (OwnWindow(display, window, windowClass))
        return GameFocus::Active;

    for (std::string_view marker : kGameClassMarkers)
    {
        if (windowClass.find(marker) != std::string::npos)
            return GameFocus::Active;
    }

    if (WindowTitle(display, window) == kGameTitle)
        return GameFocus::Active;

    return GameFocus::Inactive;
}
}

bool GameFocusSupported()
{
    return true;
}

GameFocus QueryGameFocus()
{
    Display* display = FocusDisplay();

    if (!display)
        return GameFocus::Unknown;

    gSawFocusError = false;
    const XErrorHandler previous = XSetErrorHandler(TrapFocusError);
    const GameFocus focus = Classify(display, ActiveWindow(display));
    XSync(display, False);
    XSetErrorHandler(previous);

    return gSawFocusError ? GameFocus::Unknown : focus;
}
