#include "ui/UIDraw.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "core/Logger.h"
#include "platform/PlatformShell.h"
#include "core/Feature.h"
#include "ui/UIManager.h"

namespace
{
constexpr ImVec4 kGreen{ 0.5f, 1.0f, 0.5f, 1.0f };
constexpr ImVec4 kYellow{ 1.0f, 0.8f, 0.2f, 1.0f };
constexpr ImVec4 kRed{ 1.0f, 0.3f, 0.3f, 1.0f };
constexpr double kConfigSaveDelaySeconds = 0.5;
constexpr int kTrustedMatchConfidence = 85;
constexpr float kMinDebugTableHeight = 120.0f;
}

void UIDraw::CellText(const char* text)
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

void UIDraw::DrawTitleBar(UIManager& manager, UIState&)
{
    const float titleBarHeight = 16;

    ImGui::BeginChild("TitleBar", ImVec2(0, titleBarHeight), false);

    ImGui::TextUnformatted("RuneHelper");
    ImGui::SameLine();
    ImGui::TextDisabled("v%s", RUNEHELPER_VERSION_LABEL);

    ImGui::SameLine(ImGui::GetWindowWidth() - 40.0f);

    if (ImGui::Button("_", ImVec2(16, 16)))
        manager.RequestMinimize();

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

    row("Capture");
    if (state.captureFailing)
        ImGui::TextColored(kRed, "Failing");
    else
        ImGui::TextColored(kGreen, "OK");

    if (ImGui::IsItemHovered() && state.captureFailing)
        ImGui::SetTooltip("The selected region cannot be captured. See runehelper.log for the reason.");

    row("Overlay");
    if (state.overlayAvailable)
        ImGui::TextColored(kGreen, "Ready");
    else
        ImGui::TextColored(kRed, "Unavailable");

    if (ImGui::IsItemHovered() && !state.overlayAvailable)
        ImGui::SetTooltip("The overlay window could not be created, prices are shown in the Debug Menu only.");

    row("Prices");
    if (state.priceDownloading)
        ImGui::TextColored(kYellow, "Downloading");
    else
        ImGui::TextColored(kGreen, "%zu items loaded", state.priceCount);

    row("Version");
    ImGui::Text("v%s", RUNEHELPER_VERSION);

    if (RUNEHELPER_COMMIT[0] != '\0')
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(" (%s)", RUNEHELPER_COMMIT);
    }

    if (manager.IsCheckingForUpdate())
    {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "(checking...)");
    }
    else if (manager.HasUpdate())
    {
        const std::string& url = manager.UpdateDownloadUrl();

        ImGui::SameLine();

        if (ImGui::SmallButton("Update"))
        {
            if (!OpenExternalUrl(url))
                LOG_ERROR("Could not open the update page in a browser");
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", url.empty() ? "No download link was reported" : url.c_str());
    }


    ImGui::EndTable();
    ImGui::Spacing();

    //Region
    if (!manager.HasConfig())
        return;

    AppConfig& config = manager.ConfigDraft();
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

    ImGui::Spacing();

    //PRICES
    ImGui::SeparatorText("PRICES");

    if (ImGui::Checkbox("Enable Price Search", &config.priceSearchEnabled))
    {
        configChanged = true;
        if (config.priceSearchEnabled)
            state.wantsRefreshPrices = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Matches OCR loot text against the price cache and shows prices on the overlay.");

    if (!config.priceSearchEnabled)
        ImGui::BeginDisabled();

    if (ImGui::Button("Refresh Prices"))
        state.wantsRefreshPrices = true;

    if (!config.priceSearchEnabled)
        ImGui::EndDisabled();

    ImGui::Spacing();

    if (FeatureRegistry* features = manager.Features())
    {
        ImGui::SeparatorText("FEATURES");

        for (const auto& feature : features->All())
            feature->DrawMainControls(manager);

        ImGui::Spacing();
    }

    if (configChanged)
    {
        manager.ApplyConfigDraft();

        state.configSavePending = true;
        state.configSaveAt = ImGui::GetTime() + kConfigSaveDelaySeconds;
    }

    //Bottom
    ImGui::Separator();

    const char* DenzTag = "Denz";
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(DenzTag).x);

    ImGui::TextDisabled("%s", DenzTag);
}

void UIDraw::DrawSettingsTab(UIManager& manager, UIState& state)
{
    if (!manager.HasConfig())
        return;

    AppConfig& config = manager.ConfigDraft();
    bool configChanged = false;

    ImGui::SeparatorText("PRICES");

    constexpr const char* kPriceLeagues[] = {
        "Forbidden Rites",
        "HC Forbidden Rites",
        "Runes of Aldur",
        "HC Runes of Aldur",
        "Standard",
        "Hardcore"
    };

    auto pickLeague = [&config, &configChanged, &state](std::string league)
    {
        if (league.empty() || league == config.priceLeague)
            return;

        config.priceLeague = std::move(league);
        configChanged = true;

        if (config.priceSearchEnabled)
            state.wantsRefreshPrices = true;
    };

    const bool leagueOpen = ImGui::BeginCombo("League", config.priceLeague.c_str());
    const bool leagueHovered = ImGui::IsItemHovered();

    if (leagueOpen)
    {
        for (const char* league : kPriceLeagues)
        {
            const bool selected = config.priceLeague == league;

            if (ImGui::Selectable(league, selected))
                pickLeague(league);

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();

        if (ImGui::IsWindowAppearing())
            std::snprintf(state.customLeague, sizeof(state.customLeague), "%s", config.priceLeague.c_str());

        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::InputTextWithHint(
                "##custom_league",
                "another league, then Enter",
                state.customLeague,
                sizeof(state.customLeague),
                ImGuiInputTextFlags_EnterReturnsTrue))
        {
            pickLeague(state.customLeague);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndCombo();
    }

    if (leagueHovered)
        ImGui::SetTooltip("A league missing from the list can be typed in. New leagues work as soon as the price proxy carries them.");

    constexpr const char* kPriceUnits[] = {
        "Exalted",
        "Exalted + divine",
        "Divine"
    };

    int priceUnit = static_cast<int>(config.priceUnit);

    if (ImGui::Combo("Units", &priceUnit, kPriceUnits, IM_ARRAYSIZE(kPriceUnits)))
    {
        config.priceUnit = static_cast<PriceUnit>(priceUnit);
        configChanged = true;
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Exalted + divine adds the divine value to rows worth at least one divine. Divine shows every price in divine orbs.");

    ImGui::Spacing();

    configChanged |= ImGui::InputInt("Green >= ex", &config.priceColorMedium);
    configChanged |= ImGui::InputInt("Yellow >= ex", &config.priceColorHigh);
    configChanged |= ImGui::InputInt("Red >= ex", &config.priceColorVeryHigh);
    configChanged |= ImGui::SliderInt("Refresh minutes", &config.priceRefreshMinutes, 5, 360);

    ImGui::Spacing();

    ImGui::SeparatorText("OVERLAY");

    configChanged |= ImGui::SliderInt("Offset X", &config.overlayOffsetX, -300, 500);
    configChanged |= ImGui::SliderInt("Offset Y", &config.overlayOffsetY, -200, 200);
    configChanged |= ImGui::SliderInt("Font Size", &config.overlayFontSize, 8, 48);

    configChanged |= ImGui::Checkbox("Background", &config.overlayBackground);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draws a dark plate behind the overlay text. Turn it off for bare text over the game.");

    configChanged |= ImGui::Checkbox("Outline", &config.overlayOutline);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Traces the text in black so it stays readable without a background plate.");

    ImGui::Spacing();

    ImGui::SeparatorText("HOTKEYS");

    DrawHotkeyButton(manager, state, "Toggle OCR", config.hotkeyToggleOCR);
    DrawHotkeyButton(manager, state, "Single Snapshot", config.hotkeySingleSnapshot);
    DrawHotkeyButton(manager, state, "Select Region", config.hotkeySelectRegion);

    if (configChanged)
    {
        manager.ApplyConfigDraft();

        state.configSavePending = true;
        state.configSaveAt = ImGui::GetTime() + kConfigSaveDelaySeconds;
    }
}

void UIDraw::DrawDebugTab(UIManager& manager, UIState&)
{
    if (manager.HasConfig())
    {
        AppConfig& config = manager.ConfigDraft();

        if (ImGui::Checkbox("Debug OCR", &config.debugOCR))
        {
            manager.ApplyConfigDraft();

            if (!manager.SaveConfig())
                LOG_ERROR("UI failed to autosave config");
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Writes OCR crops and recognition logs into the ocr_debug/latest folder.");

        ImGui::Spacing();
    }

    ImGui::SeparatorText("OCR DEBUG");

    const DebugData& debug = manager.GetDebugData();

    if (debug.lines.empty())
    {
        ImGui::TextDisabled("No OCR data yet.");
        return;
    }

    const float available = ImGui::GetContentRegionAvail().y;
    const ImVec2 tableSize(0.0f, available > kMinDebugTableHeight ? available : kMinDebugTableHeight);

    if (ImGui::BeginTable("ocr_debug_table", 4,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp,
        tableSize))
    {
        ImGui::TableSetupScrollFreeze(0, 1);

        ImGui::TableSetupColumn("OCR Text");
        ImGui::TableSetupColumn("Matched");
        ImGui::TableSetupColumn("Confidence");
        ImGui::TableSetupColumn("Price");

        ImGui::TableHeadersRow();

        for (const auto& line : debug.lines)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            CellText(line.ocrText.c_str());

            const bool matched = line.matchedText != "-";

            ImGui::TableSetColumnIndex(1);

            if (matched)
                CellText(line.matchedText.c_str());
            else
                ImGui::TextDisabled("not in price list");

            ImGui::TableSetColumnIndex(2);

            if (!matched)
            {
                ImGui::TextDisabled("-");

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("This item has no price on the market, so RuneHelper has nothing to show.");
            }
            else if (line.confidence >= kTrustedMatchConfidence)
            {
                ImGui::TextColored(kGreen, "%d%%", line.confidence);
            }
            else
            {
                ImGui::TextColored(kYellow, "%d%% ?", line.confidence);

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The name was guessed, not read cleanly. Verify before trusting this price.");
            }

            ImGui::TableSetColumnIndex(3);

            if (matched)
                ImGui::Text("%s", line.price.c_str());
            else
                ImGui::TextDisabled("-");
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

    state.debugTabOpen = false;
    state.featureTabOpen = false;

    if (ImGui::BeginTabBar("MainTabs"))
    {
        if (ImGui::BeginTabItem("RuneHelper"))
        {
            DrawMainTab(manager, state);
            ImGui::EndTabItem();
        }

        if (FeatureRegistry* features = manager.Features())
        {
            for (const auto& feature : features->All())
            {
                const char* title = feature->TabTitle();

                if (!title)
                    continue;

                if (ImGui::BeginTabItem(title))
                {
                    state.featureTabOpen = true;
                    feature->DrawTab(manager);
                    ImGui::EndTabItem();
                }
            }
        }

        if (ImGui::BeginTabItem("Settings"))
        {
            DrawSettingsTab(manager, state);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Debug Menu"))
        {
            state.debugTabOpen = true;
            DrawDebugTab(manager, state);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    if (state.configSavePending && ImGui::GetTime() >= state.configSaveAt && manager.HasConfig())
    {
        state.configSavePending = false;

        if (!manager.SaveConfig())
            LOG_ERROR("UI failed to autosave config");
    }
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

        manager.ApplyConfigDraft();

        if (!manager.SaveConfig())
            LOG_ERROR("UI failed to save hotkey config");

        manager.RequestRegisterHotkeys();
    }

    ImGui::PopID();
}
