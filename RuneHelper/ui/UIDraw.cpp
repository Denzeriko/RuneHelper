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

constexpr ImVec4 kGreen{ 0.5f, 1.0f, 0.5f, 1.0f };
constexpr ImVec4 kYellow{ 1.0f, 0.8f, 0.2f, 1.0f };
constexpr ImVec4 kRed{ 1.0f, 0.3f, 0.3f, 1.0f };
}

void UIDraw::DrawTitleBar(UIManager& manager, UIState& state)
{
    const float titleBarHeight = 16;

    ImGui::BeginChild("TitleBar", ImVec2(0, titleBarHeight), false);

    ImGui::TextUnformatted("RuneHelper");
    ImGui::SameLine();
    ImGui::TextDisabled("v%s", RUNEHELPER_VERSION);

    ImGui::SameLine(ImGui::GetWindowWidth() - 40.0f);

    if (ImGui::Button("_", ImVec2(16, 16)))
    {
    };// ShowWindow(hwnd_, SW_MINIMIZE);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));

    if (ImGui::Button("X", ImVec2(16, 16)))
        manager.RequestExit();

    ImGui::PopStyleColor(3);

    ImGui::EndChild();
}

void UIDraw::DrawMainTab(UIManager& manager, UIState& state)
{

    //Status
    ImGui::SeparatorText("STATUS");
    if (ImGui::BeginTable("status_table", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("OCR");

        ImGui::TableSetColumnIndex(1);

        if (state.ocrInitializing)
            ImGui::TextColored(kYellow, "Initializing...");
        else if (state.ocrFailed)
            ImGui::TextColored(kRed, "Failed");
        else if (state.ocrReady)
            ImGui::TextColored(kGreen, "Ready");
        else
            ImGui::TextDisabled("Waiting...");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Prices");

        ImGui::TableSetColumnIndex(1);

        if (state.priceDownloading)
            ImGui::TextColored(kYellow, "Downloading...");
        else
            ImGui::TextColored(kGreen, "%zu items loaded", state.priceCount);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Version");

        ImGui::TableSetColumnIndex(1);

        ImGui::Text("v%s", RUNEHELPER_VERSION);

        if (manager.updateChecker_)
        {
            if (manager.updateChecker_->IsChecking())
            {
                ImGui::SameLine();
                ImGui::TextColored(kYellow, "(checking...)");
            }
            else if (manager.updateChecker_->HasUpdate())
            {
                ImGui::SameLine();
                ImGui::TextColored(kGreen, "(update available)");
                ImGui::TextWrapped("%s", manager.updateChecker_->DownloadUrl().c_str());
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();

    //Region
    if (!manager.config_)
        ImGui::End();

    AppConfig& config = *manager.config_;

    ImGui::SeparatorText("REGION");
    if (ImGui::Button("Select Region"))
        state.wantsSelectRegion = true;

    state.regionHovered = ImGui::IsItemHovered();

    ImGui::SameLine();
    if (config.regionW > 0)
        ImGui::TextDisabled("x:%d y:%d w:%d h:%d", config.regionX, config.regionY, config.regionW, config.regionH);
    else
        ImGui::TextColored(kYellow, "No region selected");

    ImGui::Spacing();
    
    //OCR
    ImGui::SeparatorText("OCR");
    ImGui::Checkbox("Enable OCR", &config.ocrEnabled);
    ImGui::SameLine();

    if (config.ocrEnabled)
        ImGui::TextColored(kGreen, "Running");
    else
        ImGui::TextColored(kRed, "Stopped");

    ImGui::Checkbox("OCR AutoDetect Menu (Experimental)", &config.ocrAutoDetect);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Only shows the overlay when the Runeshape menu is detected.\nMay occasionally fail due to OCR inaccuracies.");

    if (ImGui::SliderInt("OCR Passes", &config.ocrPasses, 1, 6))
    {
        state.wantsOCRRebuild = true;
        manager.configManager_->Save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("More passes = better OCR recognize, but higher CPU usage\nChanging OCR passes requires OCR restart.");

    /*
    ImGui::SliderFloat("OCR Threshold", reinterpret_cast<float*>(&config_->ocrThreshold), 0.0f, 255.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Image binarization threshold.\nLower values keep more details.\nHigher values remove noise but may lose characters.");
    */

    ImGui::SliderInt("OCR interval (ms)", &config.ocrIntervalMs, 100, 2000);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lower values = faster updates but higher CPU usage.");

    ImGui::Spacing();

    //HOTKEYS
    ImGui::SeparatorText("HOTKEYS");
    //DrawHotkeyButton("Toggle OCR", config_->hotkeyToggleOCR);
    //DrawHotkeyButton("Single Snapshot", config_->hotkeySingleSnapshot);
    //DrawHotkeyButton("Select Region", config_->hotkeySelectRegion);
    ImGui::Spacing();

    //PRICES
    ImGui::SeparatorText("PRICES");
    ImGui::InputInt("Green >= ex", &config.priceColorMedium);
    ImGui::InputInt("Yellow >= ex", &config.priceColorHigh);
    ImGui::InputInt("Red >= ex", &config.priceColorVeryHigh);
    ImGui::SliderInt("Refresh minutes", &config.priceRefreshMinutes, 1, 60);

    if (ImGui::Button("Refresh Prices"))
        state.wantsRefreshPrices = true;

    ImGui::Spacing();

    //OVERLAY
    ImGui::SeparatorText("OVERLAY");
    ImGui::SliderInt("Offset X", &config.overlayOffsetX, -300, 500);
    ImGui::SliderInt("Offset Y", &config.overlayOffsetY, -200, 200);
    ImGui::SliderInt("Font Size", &config.overlayFontSize, 8, 48);
    ImGui::Spacing();

    //Bottom
    ImGui::Separator();

    if (ImGui::Button("Save"))
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

    if (state.showSaved)
    {
        auto elapsed = std::chrono::steady_clock::now() - state.savedAt;

        if (elapsed < std::chrono::seconds(1))
        {
            ImGui::SameLine();
            ImGui::Text("Saved");
        }
        else
        {
            state.showSaved = false;
        }
    }

    const char* DenzTag = "Denz";
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(DenzTag).x);

    ImGui::TextDisabled("%s", DenzTag);
}

void UIDraw::DrawDebugTab(UIManager& manager, UIState& state)
{
    ImGui::SeparatorText("OCR DEBUG");
    if (manager.GetDebugData().lines.empty())
    {
        ImGui::TextDisabled("No OCR data yet.");
        return;
    }

    if (ImGui::BeginTable("ocr_debug_table", 4,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("OCR Text");
        ImGui::TableSetupColumn("Matched");
        ImGui::TableSetupColumn("Confidence");
        ImGui::TableSetupColumn("Price");

        ImGui::TableHeadersRow();

        for (const auto& line : manager.GetDebugData().lines)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", line.ocrText.c_str());

            ImGui::TableSetColumnIndex(1);

            if (line.matchedText == "-")
                ImGui::TextDisabled("-");
            else
                ImGui::TextWrapped("%s", line.matchedText.c_str());

            ImGui::TableSetColumnIndex(2);

            if (line.confidence >= 90)
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%d%%", line.confidence);
            else if (line.confidence >= 75)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%d%%", line.confidence);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%d%%", line.confidence);

            ImGui::TableSetColumnIndex(3);

            if (line.price == "-")
                ImGui::TextDisabled("-");
            else
                ImGui::Text("%s", line.price.c_str());
        }

        ImGui::EndTable();
    }
}

void UIDraw::Draw(UIManager& manager)
{
    UIState& state = manager.state_;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    ImGui::Begin(
        "RuneHelper",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse
    );
    
    DrawTitleBar(manager, state);

    if (ImGui::BeginTabBar("MainTabs"))
    {
        if (ImGui::BeginTabItem("RuneHelper"))
        {
            DrawMainTab(manager, state);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Debug Menu"))
        {
            DrawDebugTab(manager, state);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
