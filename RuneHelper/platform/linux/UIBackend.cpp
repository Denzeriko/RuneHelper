#include "platform/UIBackend.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/Logger.h"
#include "ui/UIManager.h"

namespace
{
constexpr std::array<unsigned int, 4> kGrabModifiers = {
    0,
    LockMask,
    Mod2Mask,
    LockMask | Mod2Mask
};

enum class HotkeyAction
{
    ToggleOcr,
    SingleSnapshot,
    SelectRegion
};

struct RegisteredHotkey
{
    KeyCode keycode = 0;
    HotkeyAction action = HotkeyAction::ToggleOcr;
};
}

struct UIBackend::Impl
{
    GLFWwindow* window = nullptr;
    Display* hotkeyDisplay = nullptr;
    Window rootWindow = 0;
    UIManager* manager = nullptr;
    std::vector<RegisteredHotkey> registeredHotkeys;
    bool running = false;
    bool hotkeysAvailable = false;
    bool loggedHotkeysUnavailable = false;

    void InitHotkeyDisplay();
    void DispatchHotkeyEvents();
    void DispatchHotkeyAction(HotkeyAction action);
};

namespace
{
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

std::string UpperAscii(std::string text)
{
    for (char& ch : text)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

    return text;
}

std::string FunctionKeyName(int key)
{
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25)
        return "F" + std::to_string(key - GLFW_KEY_F1 + 1);

    // Tolerate existing configs created with Win32 VK_F1..VK_F24 defaults.
    if (key >= 0x70 && key <= 0x87)
        return "F" + std::to_string(key - 0x70 + 1);

    return {};
}

const char* SpecialKeyName(int key)
{
    switch (key)
    {
    case GLFW_KEY_SPACE: return "Space";
    case GLFW_KEY_APOSTROPHE: return "'";
    case GLFW_KEY_COMMA: return ",";
    case GLFW_KEY_MINUS: return "-";
    case GLFW_KEY_PERIOD: return ".";
    case GLFW_KEY_SLASH: return "/";
    case GLFW_KEY_SEMICOLON: return ";";
    case GLFW_KEY_EQUAL: return "=";
    case GLFW_KEY_LEFT_BRACKET: return "[";
    case GLFW_KEY_BACKSLASH: return "\\";
    case GLFW_KEY_RIGHT_BRACKET: return "]";
    case GLFW_KEY_GRAVE_ACCENT: return "`";
    case GLFW_KEY_ESCAPE: return "Escape";
    case GLFW_KEY_ENTER: return "Enter";
    case GLFW_KEY_TAB: return "Tab";
    case GLFW_KEY_BACKSPACE: return "Backspace";
    case GLFW_KEY_INSERT: return "Insert";
    case GLFW_KEY_DELETE: return "Delete";
    case GLFW_KEY_RIGHT: return "Right";
    case GLFW_KEY_LEFT: return "Left";
    case GLFW_KEY_DOWN: return "Down";
    case GLFW_KEY_UP: return "Up";
    case GLFW_KEY_PAGE_UP: return "Page Up";
    case GLFW_KEY_PAGE_DOWN: return "Page Down";
    case GLFW_KEY_HOME: return "Home";
    case GLFW_KEY_END: return "End";
    case GLFW_KEY_CAPS_LOCK: return "Caps Lock";
    case GLFW_KEY_SCROLL_LOCK: return "Scroll Lock";
    case GLFW_KEY_NUM_LOCK: return "Num Lock";
    case GLFW_KEY_PRINT_SCREEN: return "Print Screen";
    case GLFW_KEY_PAUSE: return "Pause";
    case GLFW_KEY_LEFT_SHIFT: return "Left Shift";
    case GLFW_KEY_LEFT_CONTROL: return "Left Ctrl";
    case GLFW_KEY_LEFT_ALT: return "Left Alt";
    case GLFW_KEY_LEFT_SUPER: return "Left Super";
    case GLFW_KEY_RIGHT_SHIFT: return "Right Shift";
    case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
    case GLFW_KEY_RIGHT_ALT: return "Right Alt";
    case GLFW_KEY_RIGHT_SUPER: return "Right Super";
    case GLFW_KEY_MENU: return "Menu";
    case GLFW_KEY_KP_0: return "Num 0";
    case GLFW_KEY_KP_1: return "Num 1";
    case GLFW_KEY_KP_2: return "Num 2";
    case GLFW_KEY_KP_3: return "Num 3";
    case GLFW_KEY_KP_4: return "Num 4";
    case GLFW_KEY_KP_5: return "Num 5";
    case GLFW_KEY_KP_6: return "Num 6";
    case GLFW_KEY_KP_7: return "Num 7";
    case GLFW_KEY_KP_8: return "Num 8";
    case GLFW_KEY_KP_9: return "Num 9";
    case GLFW_KEY_KP_DECIMAL: return "Num .";
    case GLFW_KEY_KP_DIVIDE: return "Num /";
    case GLFW_KEY_KP_MULTIPLY: return "Num *";
    case GLFW_KEY_KP_SUBTRACT: return "Num -";
    case GLFW_KEY_KP_ADD: return "Num +";
    case GLFW_KEY_KP_ENTER: return "Num Enter";
    case GLFW_KEY_KP_EQUAL: return "Num =";
    default: return nullptr;
    }
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

}

void UIBackend::Impl::InitHotkeyDisplay()
{
    if (IsWaylandSession())
    {
        LOG_INFO("Linux global hotkeys require X11; Wayland is not supported yet");
        loggedHotkeysUnavailable = true;
        return;
    }

    hotkeyDisplay = XOpenDisplay(nullptr);
    if (!hotkeyDisplay)
    {
        LOG_INFO("Linux global hotkeys require X11; Wayland is not supported yet");
        loggedHotkeysUnavailable = true;
        return;
    }

    rootWindow = DefaultRootWindow(hotkeyDisplay);
    hotkeysAvailable = true;
}

void UIBackend::Impl::DispatchHotkeyAction(HotkeyAction action)
{
    if (!manager)
        return;

    switch (action)
    {
    case HotkeyAction::ToggleOcr:
        manager->RequestToggleOCR();
        break;
    case HotkeyAction::SingleSnapshot:
        manager->RequestSingleSnapshot();
        break;
    case HotkeyAction::SelectRegion:
        manager->RequestSelectRegion();
        break;
    }
}

void UIBackend::Impl::DispatchHotkeyEvents()
{
    if (!hotkeysAvailable || !hotkeyDisplay)
        return;

    while (XPending(hotkeyDisplay) > 0)
    {
        XEvent event;
        XNextEvent(hotkeyDisplay, &event);

        if (event.type != KeyPress)
            continue;

        const KeyCode keycode = static_cast<KeyCode>(event.xkey.keycode);
        for (const RegisteredHotkey& hotkey : registeredHotkeys)
        {
            if (hotkey.keycode == keycode)
            {
                DispatchHotkeyAction(hotkey.action);
                break;
            }
        }
    }
}

UIBackend::UIBackend()
    : impl_(new Impl())
{
}

UIBackend::~UIBackend()
{
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool UIBackend::Init(UIManager* manager)
{
    impl_->manager = manager;

    if (!glfwInit())
    {
        LOG_ERROR("Linux UI: glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    impl_->window = glfwCreateWindow(420, 680, "RuneHelper", nullptr, nullptr);

    if (!impl_->window)
    {
        LOG_ERROR("Linux UI: glfwCreateWindow failed");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(impl_->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    SetupStyle();

    if (!ImGui_ImplGlfw_InitForOpenGL(impl_->window, true))
    {
        LOG_ERROR("Linux UI: ImGui GLFW backend init failed");
        Shutdown();
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 130"))
    {
        LOG_ERROR("Linux UI: ImGui OpenGL backend init failed");
        Shutdown();
        return false;
    }

    impl_->InitHotkeyDisplay();

    impl_->running = true;
    LOG_INFO("Linux UI backend initialized");
    return true;
}

void UIBackend::Shutdown()
{
    if (!impl_)
        return;

    impl_->running = false;

    UnregisterHotkeys();

    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    if (impl_->window)
    {
        glfwDestroyWindow(impl_->window);
        impl_->window = nullptr;
    }

    glfwTerminate();

    if (impl_->hotkeyDisplay)
    {
        XCloseDisplay(impl_->hotkeyDisplay);
        impl_->hotkeyDisplay = nullptr;
        impl_->rootWindow = 0;
        impl_->hotkeysAvailable = false;
    }
}

bool UIBackend::BeginFrame()
{
    if (!impl_ || !impl_->running || !impl_->window)
        return false;

    glfwPollEvents();
    impl_->DispatchHotkeyEvents();

    if (glfwWindowShouldClose(impl_->window))
    {
        impl_->running = false;
        return false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void UIBackend::EndFrame()
{
    if (!impl_ || !impl_->window)
        return;

    ImGui::Render();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl_->window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(impl_->window);
}

bool UIBackend::IsRunning() const
{
    return impl_ && impl_->running;
}

void UIBackend::RequestClose()
{
    if (!impl_)
        return;

    impl_->running = false;

    if (impl_->window)
        glfwSetWindowShouldClose(impl_->window, GLFW_TRUE);
}

std::string UIBackend::HotkeyToString(int key) const
{
    if (key == 0)
        return "None";

    if (std::string name = FunctionKeyName(key); !name.empty())
        return name;

    if (const char* name = SpecialKeyName(key))
        return name;

    const char* glfwName = glfwGetKeyName(key, 0);
    if (glfwName && glfwName[0] != '\0')
        return UpperAscii(glfwName);

    return "Unknown";
}

bool UIBackend::CaptureNextHotkey(int& key)
{
    if (!impl_ || !impl_->window)
        return false;

    if (glfwGetKey(impl_->window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    {
        key = 0;
        return true;
    }

    for (int glfwKey = GLFW_KEY_SPACE; glfwKey <= GLFW_KEY_LAST; ++glfwKey)
    {
        if (glfwKey == GLFW_KEY_UNKNOWN || glfwKey == GLFW_KEY_ESCAPE)
            continue;

        if (glfwGetKey(impl_->window, glfwKey) == GLFW_PRESS)
        {
            key = glfwKey;
            return true;
        }
    }

    return false;
}


void UIBackend::RegisterHotkeys(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey)
{
    if (!impl_)
        return;

    UnregisterHotkeys();

    if (!impl_->hotkeysAvailable || !impl_->hotkeyDisplay)
    {
        if (!impl_->loggedHotkeysUnavailable)
        {
            LOG_INFO("Linux global hotkeys require X11; Wayland is not supported yet");
            impl_->loggedHotkeysUnavailable = true;
        }
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

        const KeyCode keycode = XKeysymToKeycode(impl_->hotkeyDisplay, keySym);
        if (keycode == 0)
        {
            LOG_ERROR(std::string("Linux UI: failed to convert global hotkey for ") + label);
            return;
        }

        for (unsigned int modifier : kGrabModifiers)
        {
            XGrabKey(
                impl_->hotkeyDisplay,
                keycode,
                modifier,
                impl_->rootWindow,
                False,
                GrabModeAsync,
                GrabModeAsync
            );
        }

        impl_->registeredHotkeys.push_back({keycode, action});
    };

    grabHotkey(toggleOcrKey, HotkeyAction::ToggleOcr, "toggle OCR");
    grabHotkey(singleSnapshotKey, HotkeyAction::SingleSnapshot, "single snapshot");
    grabHotkey(selectRegionKey, HotkeyAction::SelectRegion, "select region");

    XSync(impl_->hotkeyDisplay, False);
    XSetErrorHandler(previousErrorHandler);

    if (gSawXGrabError)
        LOG_ERROR("Linux UI: one or more global hotkeys could not be registered");
}

void UIBackend::UnregisterHotkeys()
{
    if (!impl_ || !impl_->hotkeyDisplay || impl_->registeredHotkeys.empty())
        return;

    for (const RegisteredHotkey& hotkey : impl_->registeredHotkeys)
    {
        for (unsigned int modifier : kGrabModifiers)
            XUngrabKey(impl_->hotkeyDisplay, hotkey.keycode, modifier, impl_->rootWindow);
    }

    impl_->registeredHotkeys.clear();
    XSync(impl_->hotkeyDisplay, False);
}

void UIBackend::SetupStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.95f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
}
