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
};

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

void UIBackend::RegisterHotkeys(int, int, int)
{
}

void UIBackend::UnregisterHotkeys()
{
}
