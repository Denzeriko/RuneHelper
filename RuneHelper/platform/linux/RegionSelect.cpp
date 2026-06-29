#include "RegionSelect.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include "core/Logger.h"

namespace
{
bool IsX11Session()
{
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");

    if (sessionType && std::string(sessionType) == "wayland")
        return false;

    return true;
}

cv::Rect RectFromPoints(int x1, int y1, int x2, int y2)
{
    const int left = std::min(x1, x2);
    const int top = std::min(y1, y2);
    const int right = std::max(x1, x2);
    const int bottom = std::max(y1, y2);

    return cv::Rect(left, top, right - left, bottom - top);
}

void DrawSelectionRect(Display* display, Window root, GC gc, const cv::Rect& rect)
{
    if (rect.empty())
        return;

    XDrawRectangle(
        display,
        root,
        gc,
        rect.x,
        rect.y,
        static_cast<unsigned int>(rect.width),
        static_cast<unsigned int>(rect.height)
    );
    XFlush(display);
}
}

RegionSelector::~RegionSelector() = default;

cv::Rect RegionSelector::Select()
{
    if (!IsX11Session())
    {
        LOG_ERROR("Linux region selection requires an X11 session; Wayland is not supported yet");
        return {};
    }

    Display* display = XOpenDisplay(nullptr);

    if (!display)
    {
        LOG_ERROR("Linux region selection requires X11, but XOpenDisplay failed");
        return {};
    }

    Window root = DefaultRootWindow(display);
    Cursor cursor = XCreateFontCursor(display, XC_crosshair);

    int grabResult = XGrabPointer(
        display,
        root,
        False,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync,
        GrabModeAsync,
        None,
        cursor,
        CurrentTime
    );

    if (grabResult != GrabSuccess)
    {
        LOG_ERROR("Linux region selection failed: unable to grab X11 pointer");
        XFreeCursor(display, cursor);
        XCloseDisplay(display);
        return {};
    }

    XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);

    XGCValues values{};
    values.function = GXxor;
    values.foreground = WhitePixel(display, DefaultScreen(display));
    values.subwindow_mode = IncludeInferiors;
    values.line_width = 2;

    GC gc = XCreateGC(
        display,
        root,
        GCFunction | GCForeground | GCSubwindowMode | GCLineWidth,
        &values
    );

    LOG_INFO("Linux region selection started");

    bool dragging = false;
    bool drawn = false;
    bool cancelled = false;
    int startX = 0;
    int startY = 0;
    cv::Rect currentRect;
    cv::Rect selectedRect;

    while (true)
    {
        XEvent event{};
        XNextEvent(display, &event);

        if (event.type == ButtonPress && event.xbutton.button == Button1)
        {
            dragging = true;
            startX = event.xbutton.x_root;
            startY = event.xbutton.y_root;
            currentRect = {};
        }
        else if (event.type == MotionNotify && dragging)
        {
            while (XCheckTypedEvent(display, MotionNotify, &event))
            {
            }

            if (drawn)
                DrawSelectionRect(display, root, gc, currentRect);

            currentRect = RectFromPoints(startX, startY, event.xmotion.x_root, event.xmotion.y_root);

            DrawSelectionRect(display, root, gc, currentRect);
            drawn = true;
        }
        else if (event.type == ButtonRelease && event.xbutton.button == Button1 && dragging)
        {
            if (drawn)
                DrawSelectionRect(display, root, gc, currentRect);

            selectedRect = RectFromPoints(startX, startY, event.xbutton.x_root, event.xbutton.y_root);
            break;
        }
        else if (event.type == KeyPress)
        {
            KeySym key = XLookupKeysym(&event.xkey, 0);

            if (key == XK_Escape)
            {
                if (drawn)
                    DrawSelectionRect(display, root, gc, currentRect);

                cancelled = true;
                break;
            }
        }
    }

    XFreeGC(display, gc);
    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);
    XCloseDisplay(display);

    if (cancelled)
    {
        LOG_INFO("Linux region selection cancelled");
        return {};
    }

    if (selectedRect.width < 2 || selectedRect.height < 2)
    {
        LOG_ERROR("Linux region selection ignored: selected rectangle is too small");
        return {};
    }

    LOG_INFO(
        "Linux region selected: x=" + std::to_string(selectedRect.x) +
        " y=" + std::to_string(selectedRect.y) +
        " w=" + std::to_string(selectedRect.width) +
        " h=" + std::to_string(selectedRect.height)
    );

    return selectedRect;
}
