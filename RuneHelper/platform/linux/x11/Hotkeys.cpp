#include "platform/linux/LinuxHotkeys.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "core/Logger.h"

namespace
{
constexpr std::array<unsigned int, 4> kGrabModifiers = {
    0,
    LockMask,
    Mod2Mask,
    LockMask | Mod2Mask
};

struct RegisteredHotkey
{
    KeyCode keycode = 0;
    HotkeyAction action = HotkeyAction::ToggleOcr;
};

bool IsWaylandSession()
{
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");
    if (!sessionType)
        return false;

    std::string value(sessionType);
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    return value == "wayland";
}

bool gSawXGrabError = false;

int TrapXGrabError(Display*, XErrorEvent*)
{
    gSawXGrabError = true;
    return 0;
}

KeySym FunctionKeySym(int key)
{
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
        return XK_F1 + (key - GLFW_KEY_F1);

    // Tolerate existing configs created with Win32 VK_F1..VK_F12 defaults.
    if (key >= 0x70 && key <= 0x7B)
        return XK_F1 + (key - 0x70);

    return NoSymbol;
}

KeySym HotkeyToKeySym(int key)
{
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        return XK_A + (key - GLFW_KEY_A);

    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        return XK_0 + (key - GLFW_KEY_0);

    if (KeySym functionKey = FunctionKeySym(key); functionKey != NoSymbol)
        return functionKey;

    switch (key)
    {
    case GLFW_KEY_ESCAPE: return XK_Escape;
    case GLFW_KEY_SPACE: return XK_space;
    case GLFW_KEY_ENTER: return XK_Return;
    case GLFW_KEY_TAB: return XK_Tab;
    default: return NoSymbol;
    }
}

class X11Hotkeys final : public LinuxHotkeys
{
public:
    void Init() override;
    void Shutdown() override;

    void Poll(const Dispatch& dispatch) override;

    void Register(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey) override;
    void Unregister() override;

private:
    void LogUnavailable();

    Display* display_ = nullptr;
    Window rootWindow_ = 0;
    std::vector<RegisteredHotkey> registered_;
    bool available_ = false;
    bool loggedUnavailable_ = false;
};

void X11Hotkeys::LogUnavailable()
{
    if (loggedUnavailable_)
        return;

    LOG_INFO("Linux global hotkeys require X11; build with -DRUNEHELPER_LINUX_BACKEND=wayland for Wayland");
    loggedUnavailable_ = true;
}

void X11Hotkeys::Init()
{
    if (IsWaylandSession())
    {
        LogUnavailable();
        return;
    }

    display_ = XOpenDisplay(nullptr);
    if (!display_)
    {
        LogUnavailable();
        return;
    }

    rootWindow_ = DefaultRootWindow(display_);
    available_ = true;
}

void X11Hotkeys::Shutdown()
{
    Unregister();

    if (display_)
    {
        XCloseDisplay(display_);
        display_ = nullptr;
        rootWindow_ = 0;
        available_ = false;
    }
}

void X11Hotkeys::Poll(const Dispatch& dispatch)
{
    if (!available_ || !display_)
        return;

    while (XPending(display_) > 0)
    {
        XEvent event;
        XNextEvent(display_, &event);

        if (event.type != KeyPress)
            continue;

        const KeyCode keycode = static_cast<KeyCode>(event.xkey.keycode);
        for (const RegisteredHotkey& hotkey : registered_)
        {
            if (hotkey.keycode == keycode)
            {
                dispatch(hotkey.action);
                break;
            }
        }
    }
}

void X11Hotkeys::Register(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey)
{
    Unregister();

    if (!available_ || !display_)
    {
        LogUnavailable();
        return;
    }

    gSawXGrabError = false;
    XErrorHandler previousErrorHandler = XSetErrorHandler(TrapXGrabError);

    auto grabHotkey = [this](int key, HotkeyAction action, const char* label)
    {
        if (key == 0)
            return;

        const KeySym keySym = HotkeyToKeySym(key);
        if (keySym == NoSymbol)
        {
            LOG_ERROR(std::string("Linux UI: unsupported global hotkey for ") + label + ": " + std::to_string(key));
            return;
        }

        const KeyCode keycode = XKeysymToKeycode(display_, keySym);
        if (keycode == 0)
        {
            LOG_ERROR(std::string("Linux UI: failed to convert global hotkey for ") + label);
            return;
        }

        for (unsigned int modifier : kGrabModifiers)
        {
            XGrabKey(
                display_,
                keycode,
                modifier,
                rootWindow_,
                False,
                GrabModeAsync,
                GrabModeAsync
            );
        }

        registered_.push_back({keycode, action});
    };

    grabHotkey(toggleOcrKey, HotkeyAction::ToggleOcr, "toggle OCR");
    grabHotkey(singleSnapshotKey, HotkeyAction::SingleSnapshot, "single snapshot");
    grabHotkey(selectRegionKey, HotkeyAction::SelectRegion, "select region");

    XSync(display_, False);
    XSetErrorHandler(previousErrorHandler);

    if (gSawXGrabError)
        LOG_ERROR("Linux UI: one or more global hotkeys could not be registered");
}

void X11Hotkeys::Unregister()
{
    if (!display_ || registered_.empty())
        return;

    for (const RegisteredHotkey& hotkey : registered_)
    {
        for (unsigned int modifier : kGrabModifiers)
            XUngrabKey(display_, hotkey.keycode, modifier, rootWindow_);
    }

    registered_.clear();
    XSync(display_, False);
}
}

std::unique_ptr<LinuxHotkeys> CreateLinuxHotkeys()
{
    return std::make_unique<X11Hotkeys>();
}

int RunLinuxHotkeyClient(int, char**)
{
    return -1;
}
