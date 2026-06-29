#include "ui/UIManager.h"

#include <algorithm>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/Logger.h"
#include "core/Version.h"

namespace
{
GLFWwindow* g_window = nullptr;

const char* OcrStatusText(bool initializing, bool ready, bool failed)
{
    if (initializing)
        return "Initializing";

    if (failed)
        return "Failed";

    if (ready)
        return "Ready";

    return "Waiting";
}
}

UIManager::~UIManager()
{
    Shutdown();
}

bool UIManager::Init(AppConfig* config, ConfigManager* configManager)
{
    config_ = config;
    configManager_ = configManager;

    if (!glfwInit())
    {
        LOG_ERROR("Linux UI: glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    g_window = glfwCreateWindow(420, 680, "RuneHelper", nullptr, nullptr);

    if (!g_window)
    {
        LOG_ERROR("Linux UI: glfwCreateWindow failed");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(g_window, true))
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

    running_ = true;
    LOG_INFO("Linux UI initialized");
    return true;
}

void UIManager::Shutdown()
{
    running_ = false;

    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    if (g_window)
    {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }

    glfwTerminate();
}

void UIManager::SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed)
{
    ocrInitializing_ = ocrInitializing;
    ocrReady_ = ocrReady;
    ocrFailed_ = ocrFailed;
}

void UIManager::Pump()
{
    if (!running_ || !g_window)
        return;

    glfwPollEvents();

    if (glfwWindowShouldClose(g_window))
    {
        running_ = false;
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(g_window, &width, &height);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)), ImGuiCond_Always);

    ImGui::Begin(
        "RuneHelper",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse
    );

    ImGui::Text("RuneHelper %s", RUNEHELPER_VERSION);
    ImGui::Separator();

    ImGui::Text("OCR: %s", OcrStatusText(ocrInitializing_, ocrReady_, ocrFailed_));

    if (priceDownloading_)
        ImGui::Text("Prices: Downloading...");
    else
        ImGui::Text("Prices: Loaded (%zu items)", priceCount_);

    ImGui::Separator();

    if (config_)
    {
        ImGui::Text("Region");

        if (ImGui::Button("Select Region"))
            wantsSelectRegion_ = true;

        ImGui::SameLine();

        if (config_->regionW <= 0)
            ImGui::Text("No region selected");
        else
            ImGui::Text("Region selected");

        ImGui::Separator();
        ImGui::Text("OCR");

        ImGui::Checkbox("OCR enabled", &config_->ocrEnabled);
        ImGui::Checkbox("Auto-detect menu", &config_->ocrAutoDetect);
        ImGui::SliderFloat("OCR threshold", &config_->ocrThreshold, 0.0f, 255.0f);
        ImGui::SliderInt("OCR interval ms", &config_->ocrIntervalMs, 100, 2000);

        ImGui::Separator();
        ImGui::Text("Overlay");

        ImGui::SliderInt("Offset X", &config_->overlayOffsetX, -300, 500);
        ImGui::SliderInt("Offset Y", &config_->overlayOffsetY, -200, 200);
        ImGui::SliderInt("Font size", &config_->overlayFontSize, 8, 48);

        ImGui::Separator();
        ImGui::Text("Prices");

        ImGui::InputInt("Green ex", &config_->priceColorMedium);
        ImGui::InputInt("Yellow ex", &config_->priceColorHigh);
        ImGui::InputInt("Red ex", &config_->priceColorVeryHigh);
        ImGui::SliderInt("Refresh minutes", &config_->priceRefreshMinutes, 1, 60);

        config_->ocrIntervalMs = std::clamp(config_->ocrIntervalMs, 100, 2000);
        config_->overlayFontSize = std::clamp(config_->overlayFontSize, 8, 48);
        config_->priceRefreshMinutes = std::clamp(config_->priceRefreshMinutes, 1, 60);
        config_->priceColorMedium = std::max(0, config_->priceColorMedium);
        config_->priceColorHigh = std::max(0, config_->priceColorHigh);
        config_->priceColorVeryHigh = std::max(0, config_->priceColorVeryHigh);

        if (ImGui::Button("Refresh Prices"))
            LOG_INFO("Linux refresh prices button pressed; action is not wired yet");

        ImGui::SameLine();

        if (ImGui::Button("Save Config"))
        {
            if (configManager_ && configManager_->Save())
            {
                showSaved_ = true;
                savedAt_ = std::chrono::steady_clock::now();
                LOG_INFO("Linux UI saved config");
            }
            else
            {
                LOG_ERROR("Linux UI failed to save config");
            }
        }

        if (showSaved_)
        {
            auto elapsed = std::chrono::steady_clock::now() - savedAt_;

            if (elapsed < std::chrono::seconds(1))
            {
                ImGui::SameLine();
                ImGui::Text("Saved");
            }
            else
            {
                showSaved_ = false;
            }
        }
    }

    ImGui::Separator();

    if (ImGui::Button("Exit"))
        running_ = false;

    ImGui::End();

    ImGui::Render();

    glfwGetFramebufferSize(g_window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_window);
}

bool UIManager::IsRunning() const
{
    return running_;
}

void UIManager::SetUpdateChecker(UpdateChecker* checker)
{
    updateChecker_ = checker;
}

bool UIManager::WantsSelectRegion()
{
    bool value = wantsSelectRegion_;
    wantsSelectRegion_ = false;
    return value;
}

bool UIManager::WantsRefreshPrices()
{
    bool value = wantsRefreshPrices_;
    wantsRefreshPrices_ = false;
    return value;
}

bool UIManager::IsRegionHovered() const
{
    return false;
}

void UIManager::SetPriceStatus(bool downloading, size_t priceCount)
{
    priceDownloading_ = downloading;
    priceCount_ = priceCount;
}
