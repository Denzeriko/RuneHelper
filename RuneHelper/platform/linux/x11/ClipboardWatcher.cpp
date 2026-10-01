#include "platform/ClipboardWatcher.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <chrono>

struct ClipboardWatcher::Impl
{
    Display* display = nullptr;
    Window window = 0;
    Atom clipboard = 0;
    Atom utf8 = 0;
    Atom property = 0;
    int eventBase = 0;
    Time requested = 0;
    bool pending = false;
    std::chrono::steady_clock::time_point deadline{};
};

ClipboardWatcher::ClipboardWatcher() : impl_(std::make_unique<Impl>()) {}

ClipboardWatcher::~ClipboardWatcher()
{
    Stop();
}

bool ClipboardWatcher::Start()
{
    if (Running())
        return true;

    impl_->display = XOpenDisplay(nullptr);

    if (!impl_->display)
        return false;

    int errorBase = 0;

    if (!XFixesQueryExtension(impl_->display, &impl_->eventBase, &errorBase))
    {
        Stop();
        return false;
    }

    impl_->window = XCreateSimpleWindow(impl_->display, DefaultRootWindow(impl_->display), 0, 0, 1, 1, 0, 0, 0);
    impl_->clipboard = XInternAtom(impl_->display, "CLIPBOARD", False);
    impl_->utf8 = XInternAtom(impl_->display, "UTF8_STRING", False);
    impl_->property = XInternAtom(impl_->display, "RUNEHELPER_CLIPBOARD", False);
    XFixesSelectSelectionInput(
        impl_->display,
        impl_->window,
        impl_->clipboard,
        XFixesSetSelectionOwnerNotifyMask | XFixesSelectionWindowDestroyNotifyMask | XFixesSelectionClientCloseNotifyMask
    );
    XFlush(impl_->display);
    return true;
}

void ClipboardWatcher::Stop()
{
    if (impl_->display)
    {
        if (impl_->window)
            XDestroyWindow(impl_->display, impl_->window);

        XCloseDisplay(impl_->display);
        impl_->display = nullptr;
    }

    impl_->window = 0;
    impl_->pending = false;
}

bool ClipboardWatcher::Running() const
{
    return impl_->display != nullptr;
}

std::optional<std::string> ClipboardWatcher::Poll()
{
    if (!Running())
        return std::nullopt;

    std::optional<std::string> result;

    while (XPending(impl_->display))
    {
        XEvent event{};
        XNextEvent(impl_->display, &event);

        if (event.type == impl_->eventBase + XFixesSelectionNotify)
        {
            const auto& changed = reinterpret_cast<const XFixesSelectionNotifyEvent&>(event);
            result = std::string{};
            impl_->pending = changed.owner != None;

            if (impl_->pending)
            {
                result.reset();
                impl_->requested = changed.timestamp;
                impl_->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                XConvertSelection(impl_->display, impl_->clipboard, impl_->utf8, impl_->property, impl_->window, impl_->requested);
            }
        }
        else if (event.type == SelectionNotify && impl_->pending && event.xselection.time == impl_->requested)
        {
            impl_->pending = false;
            result = std::string{};

            if (event.xselection.property == None)
                continue;

            Atom type = 0;
            int format = 0;
            unsigned long count = 0;
            unsigned long remaining = 0;
            unsigned char* bytes = nullptr;
            const int status = XGetWindowProperty(
                impl_->display,
                impl_->window,
                impl_->property,
                0,
                static_cast<long>(kMaxClipboardText / 4 + 1),
                True,
                AnyPropertyType,
                &type,
                &format,
                &count,
                &remaining,
                &bytes
            );

            if (status == Success && type == impl_->utf8 && format == 8 && remaining == 0 && count <= kMaxClipboardText && bytes)
                result = std::string(reinterpret_cast<const char*>(bytes), count);

            if (bytes)
                XFree(bytes);

            XDeleteProperty(impl_->display, impl_->window, impl_->property);
        }
    }

    XFlush(impl_->display);

    if (impl_->pending && std::chrono::steady_clock::now() >= impl_->deadline)
    {
        impl_->pending = false;
        result = std::string{};
    }

    return result;
}
