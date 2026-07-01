#include "platform/UIBackend.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/Logger.h"

struct UIBackend::Impl
{
    GLFWwindow* window = nullptr;
    bool running = false;
    bool loggedGlobalHotkeysUnsupported = false;
};

namespace
{
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

bool UIBackend::Init(UIManager*)
{
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

    impl_->running = true;
    LOG_INFO("Linux UI backend initialized");
    return true;
}

void UIBackend::Shutdown()
{
    if (!impl_)
        return;

    impl_->running = false;

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
}

bool UIBackend::BeginFrame()
{
    if (!impl_ || !impl_->running || !impl_->window)
        return false;

    glfwPollEvents();

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


void UIBackend::RegisterHotkeys(int, int, int)
{
    if (impl_ && !impl_->loggedGlobalHotkeysUnsupported)
    {
        LOG_INFO("Linux global hotkeys are not implemented yet");
        impl_->loggedGlobalHotkeysUnsupported = true;
    }
}

void UIBackend::UnregisterHotkeys()
{
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

