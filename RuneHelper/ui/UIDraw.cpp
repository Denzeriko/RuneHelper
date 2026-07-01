#include "ui/UIDraw.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>

#include <imgui.h>

#include "core/Logger.h"
#include "ui/UIManager.h"
#include "core/Helpers.h"

namespace
{
constexpr ImVec4 kGreen{ 0.5f, 1.0f, 0.5f, 1.0f };
constexpr ImVec4 kYellow{ 1.0f, 0.8f, 0.2f, 1.0f };
constexpr ImVec4 kRed{ 1.0f, 0.3f, 0.3f, 1.0f };
}

void UIDraw::DrawTitleBar(UIManager& manager, UIState&)
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

    if (!ImGui::BeginTable(
        "status_table",
        2,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp))
    {
        return;
    }

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("Value");

    auto row = [](const char* name)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", name);
            ImGui::TableSetColumnIndex(1);
        };

    row("OCR");

    if (state.ocrInitializing)
        ImGui::TextColored(kYellow, "Initializing");
    else if (state.ocrFailed)
        ImGui::TextColored(kRed, "Failed");
    else if (state.ocrReady)
        ImGui::TextColored(kGreen, "Ready");
    else
        ImGui::TextDisabled("Waiting");

    row("Prices");
    if (state.priceDownloading)
        ImGui::TextColored(kYellow, "Downloading");
    else
        ImGui::TextColored(kGreen, "%zu items loaded", state.priceCount);

    row("Version");
    ImGui::Text("v%s", RUNEHELPER_VERSION);
    if (manager.IsCheckingForUpdate())
    {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "(checking...)");
    }
    else if (manager.HasUpdate())
    {
        ImGui::SameLine();
        ImGui::TextColored(kGreen, "(update available)");
        ImGui::TextWrapped("%s", manager.UpdateDownloadUrl().c_str());
    }


    ImGui::EndTable();
    ImGui::Spacing();
    /*
    row("CPU");
    ImVec4 cpuColor = kGreen;
    if (state_.cpuUsagePercent > 20.0)
        cpuColor = kYellow;

    if (state_.cpuUsagePercent > 50.0)
        cpuColor = kRed;

    ImGui::TextColored(cpuColor, "%.1f%%", state_.cpuUsagePercent);
    row("RAM");
    ImGui::Text("%zu MB", state.memoryUsageMb);

    ImGui::EndTable();
    ImGui::Spacing();
    */
    //Region
    if (!manager.HasConfig())
        return;

    std::lock_guard configLock(manager.ConfigMutex());
    AppConfig& config = manager.Config();
    bool configChanged = false;

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
    configChanged |= ImGui::Checkbox("Enable OCR", &config.ocrEnabled);
    ImGui::SameLine();

    if (config.ocrEnabled)
        ImGui::TextColored(kGreen, "Running");
    else
        ImGui::TextColored(kRed, "Stopped");

    configChanged |= ImGui::Checkbox("OCR AutoDetect Menu (Experimental)", &config.ocrAutoDetect);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Only shows the overlay when the Runeshape menu is detected.\nMay occasionally fail due to OCR inaccuracies.");

    if (ImGui::SliderInt("OCR Passes", &config.ocrPasses, 1, 6))
    {
        configChanged = true;
        state.wantsOCRRebuild = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("More passes = better OCR recognize, but higher CPU usage\nChanging OCR passes requires OCR restart.");

    /*
    ImGui::SliderFloat("OCR Threshold", reinterpret_cast<float*>(&config_->ocrThreshold), 0.0f, 255.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Image binarization threshold.\nLower values keep more details.\nHigher values remove noise but may lose characters.");
    */

    configChanged |= ImGui::SliderInt("OCR interval (ms)", &config.ocrIntervalMs, 100, 2000);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lower values = faster updates but higher CPU usage.");

    ImGui::Spacing();

    //HOTKEYS
    ImGui::SeparatorText("HOTKEYS");
    DrawHotkeyButton(manager, state, "Toggle OCR", config.hotkeyToggleOCR);
    DrawHotkeyButton(manager, state, "Single Snapshot", config.hotkeySingleSnapshot);
    DrawHotkeyButton(manager, state, "Select Region", config.hotkeySelectRegion);
    ImGui::Spacing();

    //PRICES
    ImGui::SeparatorText("PRICES");
    configChanged |= ImGui::InputInt("Green >= ex", &config.priceColorMedium);
    configChanged |= ImGui::InputInt("Yellow >= ex", &config.priceColorHigh);
    configChanged |= ImGui::InputInt("Red >= ex", &config.priceColorVeryHigh);
    configChanged |= ImGui::SliderInt("Refresh minutes", &config.priceRefreshMinutes, 1, 60);

    if (ImGui::Button("Refresh Prices"))
        state.wantsRefreshPrices = true;

    ImGui::Spacing();

    //OVERLAY
    ImGui::SeparatorText("OVERLAY");
    configChanged |= ImGui::SliderInt("Offset X", &config.overlayOffsetX, -300, 500);
    configChanged |= ImGui::SliderInt("Offset Y", &config.overlayOffsetY, -200, 200);
    configChanged |= ImGui::SliderInt("Font Size", &config.overlayFontSize, 8, 48);
    ImGui::Spacing();

    if (configChanged)
    {
        ConfigManager::Normalize(config);

        if (!manager.SaveConfig())
            LOG_ERROR("UI failed to autosave config");
    }

    //Bottom
    ImGui::Separator();

    /*
    if (state.showSaved)
    {
        auto elapsed = std::chrono::steady_clock::now() - state.savedAt;

        if (elapsed < std::chrono::seconds(1))
        {
            ImGui::Text("Auto-saved");
        }
        else
        {
            state.showSaved = false;
        }
    }
    */

    const char* DenzTag = "Denz";
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(DenzTag).x);

    ImGui::TextDisabled("%s", DenzTag);
}

void UIDraw::DrawDebugTab(UIManager& manager, UIState&)
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
    UIState& state = manager.State();

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

void UIDraw::DrawHotkeyButton(UIManager& manager, UIState& state, const char* label, int& key)
{
    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::SameLine(220.0f);

    const bool capturing = state.waitingForHotkey == &key;
    const std::string text = capturing ? "Press any key..." : manager.HotkeyToString(key);

    if (ImGui::Button(text.c_str(), ImVec2(180.0f, 0.0f)))
    {
        state.waitingForHotkey = &key;
        state.hotkeyCaptureSkipFrame = true;
    }

    if (!capturing)
    {
        ImGui::PopID();
        return;
    }

    if (state.hotkeyCaptureSkipFrame)
    {
        state.hotkeyCaptureSkipFrame = false;
        ImGui::PopID();
        return;
    }

    if (manager.CaptureNextHotkey(key))
    {
        state.waitingForHotkey = nullptr;

        if (!manager.SaveConfig())
            LOG_ERROR("UI failed to save hotkey config");

        manager.RequestRegisterHotkeys();
    }

    ImGui::PopID();
}
