#include "ui/UIDraw.h"

#include <algorithm>
#include <chrono>

#include <imgui.h>

#include "core/Logger.h"
#include "core/Version.h"
#include "ui/UIManager.h"

namespace
{
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

void ClampConfig(AppConfig& config)
{
    config.ocrIntervalMs = std::clamp(config.ocrIntervalMs, 100, 2000);
    config.overlayFontSize = std::clamp(config.overlayFontSize, 8, 48);
    config.priceRefreshMinutes = std::clamp(config.priceRefreshMinutes, 1, 60);
    config.priceColorMedium = std::max(0, config.priceColorMedium);
    config.priceColorHigh = std::max(0, config.priceColorHigh);
    config.priceColorVeryHigh = std::max(0, config.priceColorVeryHigh);
}
}

void UIDraw::Draw(UIManager& manager)
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    ImGui::Begin(
        "RuneHelper",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse
    );

    ImGui::Text("RuneHelper %s", RUNEHELPER_VERSION);
    ImGui::Separator();

    ImGui::Text("OCR: %s", OcrStatusText(manager.ocrInitializing_, manager.ocrReady_, manager.ocrFailed_));

    if (manager.priceDownloading_)
        ImGui::Text("Prices: Downloading...");
    else
        ImGui::Text("Prices: Loaded (%zu items)", manager.priceCount_);

    ImGui::Separator();

    if (manager.config_)
    {
        AppConfig& config = *manager.config_;

        ImGui::Text("Region");

        if (ImGui::Button("Select Region"))
            manager.wantsSelectRegion_ = true;

        manager.regionHovered_ = ImGui::IsItemHovered();

        ImGui::SameLine();

        if (config.regionW <= 0 || config.regionH <= 0)
            ImGui::Text("No region selected");
        else
            ImGui::Text("%d,%d %dx%d", config.regionX, config.regionY, config.regionW, config.regionH);

        ImGui::Separator();
        ImGui::Text("OCR");

        ImGui::Checkbox("OCR enabled", &config.ocrEnabled);
        ImGui::Checkbox("Auto-detect menu", &config.ocrAutoDetect);
        ImGui::Checkbox("Debug OCR", &config.debugOCR);
        ImGui::SliderFloat("OCR threshold", &config.ocrThreshold, 0.0f, 255.0f);
        ImGui::SliderInt("OCR interval ms", &config.ocrIntervalMs, 100, 2000);

        if (ImGui::Button("Test OCR"))
            manager.wantsTestOcr_ = true;

        ImGui::SameLine();

        if (ImGui::Button("Reset OCR engine"))
            manager.wantsResetOcr_ = true;

        ImGui::Separator();
        ImGui::Text("Overlay");

        ImGui::SliderInt("Offset X", &config.overlayOffsetX, -300, 500);
        ImGui::SliderInt("Offset Y", &config.overlayOffsetY, -200, 200);
        ImGui::SliderInt("Font size", &config.overlayFontSize, 8, 48);

        ImGui::Separator();
        ImGui::Text("Prices");

        ImGui::InputInt("Green ex", &config.priceColorMedium);
        ImGui::InputInt("Yellow ex", &config.priceColorHigh);
        ImGui::InputInt("Red ex", &config.priceColorVeryHigh);
        ImGui::SliderInt("Refresh minutes", &config.priceRefreshMinutes, 1, 60);

        ClampConfig(config);

        if (ImGui::Button("Refresh Prices"))
            manager.wantsRefreshPrices_ = true;

        ImGui::SameLine();

        if (ImGui::Button("Save Config"))
        {
            if (manager.configManager_ && manager.configManager_->Save())
            {
                manager.MarkSaved();
                LOG_INFO("UI saved config");
            }
            else
            {
                LOG_ERROR("UI failed to save config");
            }
        }

        if (manager.showSaved_)
        {
            auto elapsed = std::chrono::steady_clock::now() - manager.savedAt_;

            if (elapsed < std::chrono::seconds(1))
            {
                ImGui::SameLine();
                ImGui::Text("Saved");
            }
            else
            {
                manager.showSaved_ = false;
            }
        }
    }

    ImGui::Separator();

    if (manager.updateChecker_)
    {
        if (manager.updateChecker_->IsChecking())
        {
            ImGui::Text("Checking for updates...");
        }
        else if (manager.updateChecker_->HasUpdate())
        {
            ImGui::Text("Update available: %s", manager.updateChecker_->LatestVersion().c_str());
            ImGui::TextWrapped("%s", manager.updateChecker_->DownloadUrl().c_str());
        }
    }

    if (ImGui::Button("Exit"))
        manager.RequestExit();

    ImGui::End();
}
