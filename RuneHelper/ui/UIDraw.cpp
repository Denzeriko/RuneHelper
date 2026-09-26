#include "ui/UIDraw.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "core/Feature.h"
#include "core/Logger.h"
#include "core/UpdateChecker.h"
#include "platform/GameFocus.h"
#include "platform/PlatformShell.h"
#include "price/ResolvedPrice.h"
#include "ui/UIManager.h"
#include "ui/UiScale.h"
#include "ui/UiTooltip.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

namespace
{
constexpr ImVec4 kGreen{ 0.5f, 1.0f, 0.5f, 1.0f };
constexpr ImVec4 kYellow{ 1.0f, 0.8f, 0.2f, 1.0f };
constexpr ImVec4 kRed{ 1.0f, 0.3f, 0.3f, 1.0f };
constexpr float kMinDebugTableHeight = 120.0f;
constexpr const char* kNewIssueUrl = "https://github.com/Denzeriko/RuneHelper/issues/new?template=ocr-problem.yml";

const std::vector<GameLanguage>& ReadableLanguages()
{
    static const std::vector<GameLanguage> languages = []
    {
        std::vector<GameLanguage> found;

        for (const GameLanguage& language : kGameLanguages)
        {
            if (!EmbeddedTextModel(language.code).empty())
                found.push_back(language);
        }

        return found;
    }();

    return languages;
}

bool DrawGameLanguage(AppConfig& config)
{
    const std::vector<GameLanguage>& languages = ReadableLanguages();

    if (languages.size() < 2)
        return false;

    const auto current = std::find_if(
        languages.begin(),
        languages.end(),
        [&config](const GameLanguage& language) { return language.code == config.gameLanguage; }
    );

    bool changed = false;

    if (ImGui::BeginCombo("Game language", current != languages.end() ? current->name.data() : config.gameLanguage.c_str()))
    {
        for (const GameLanguage& language : languages)
        {
            const bool selected = language.code == config.gameLanguage;

            if (ImGui::Selectable(language.name.data(), selected) && !selected)
            {
                config.gameLanguage = std::string(language.code);
                changed = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }

    return changed;
}

void DrawTitleBar(UIManager& ui)
{
    UIState& state = ui.State();
    const float titleBarHeight = UiScaled(16.0f);
    const ImVec2 titleButton(UiScaled(16.0f), UiScaled(16.0f));

    ImGui::BeginChild("TitleBar", ImVec2(0, titleBarHeight), false);

    ImGui::TextUnformatted("RuneHelper");
    ImGui::SameLine();
    ImGui::TextDisabled("v%s", RUNEHELPER_VERSION_LABEL);

    ImGui::SameLine(ImGui::GetWindowWidth() - UiScaled(40.0f));
    state.titleButtonsLeft = ImGui::GetCursorScreenPos().x;

    if (ImGui::Button("_", titleButton))
        ui.Minimize();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));

    if (ImGui::Button("X", titleButton))
        ui.Exit();

    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    state.titleBarBottom = ImGui::GetItemRectMax().y;
}

void StatusRow(const char* name)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", name);
    ImGui::TableSetColumnIndex(1);
}

void DrawOcrStatus(const OcrStatus& status)
{
    if (status.waitingForGame && status.state == OcrState::Ready)
    {
        ImGui::TextColored(kYellow, "Paused");

        if (ImGui::IsItemHovered())
            UiTooltip("Path of Exile is not the active window. This pause can be turned off in the Debug Menu.");

        return;
    }

    switch (status.state)
    {
    case OcrState::Initializing: ImGui::TextColored(kYellow, "Initializing"); break;
    case OcrState::Failed: ImGui::TextColored(kRed, "Failed"); break;
    case OcrState::Ready: ImGui::TextColored(kGreen, "Ready"); break;
    case OcrState::Stopped: ImGui::TextDisabled("Waiting"); break;
    }
}

void DrawReleasePageButton(const UpdateChecker& updates, const char* label)
{
    const std::string url = updates.DownloadUrl();

    if (ImGui::SmallButton(label))
    {
        if (!OpenExternalUrl(url))
            LOG_ERROR("Could not open the update page in a browser");
    }

    if (ImGui::IsItemHovered())
        UiTooltip(url.empty() ? "No download link was reported" : url.c_str());
}

void DrawUpdate(UIManager& ui)
{
    const UpdateChecker& updates = ui.Updates();

    switch (updates.Install())
    {
    case UpdateInstall::Downloading: ImGui::TextColored(kYellow, "Downloading %d%%", updates.InstallPercent()); return;
    case UpdateInstall::Installing:
    case UpdateInstall::Installed: ImGui::TextColored(kYellow, "Installing"); return;
    case UpdateInstall::Failed:
        ImGui::TextColored(kRed, "Update failed");

        if (ImGui::IsItemHovered())
            UiTooltip("See runehelper.log");

        ImGui::SameLine();
        DrawReleasePageButton(updates, "Open Page");
        return;
    case UpdateInstall::Idle: break;
    }

    if (!updates.CanInstall())
    {
        DrawReleasePageButton(updates, "Update");
        return;
    }

    const std::string label = "Update to " + updates.LatestVersion();

    if (ImGui::SmallButton(label.c_str()))
        ui.State().requests.installUpdate = true;

    if (ImGui::IsItemHovered())
        UiTooltip("Downloads the new version and restarts RuneHelper");
}

void DrawVersion(UIManager& ui)
{
    const UpdateChecker& updates = ui.Updates();

    ImGui::Text("v%s", RUNEHELPER_VERSION);

    if (RUNEHELPER_COMMIT[0] != '\0')
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(" (%s)", RUNEHELPER_COMMIT);
    }

    if (updates.IsChecking())
    {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "(checking...)");
        return;
    }

    if (!updates.HasUpdate())
        return;

    ImGui::SameLine();
    DrawUpdate(ui);
}

bool DrawStatusSection(UIManager& ui)
{
    const UIState& state = ui.State();

    ImGui::SeparatorText("STATUS");

    if (!ImGui::BeginTable("status_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return false;

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, UiScaled(95.0f));
    ImGui::TableSetupColumn("Value");

    StatusRow("OCR");
    DrawOcrStatus(state.ocr);

    StatusRow("Capture");
    if (state.ocr.captureFailing)
        ImGui::TextColored(kRed, "Failing");
    else
        ImGui::TextColored(kGreen, "OK");

    if (ImGui::IsItemHovered() && state.ocr.captureFailing)
        UiTooltip("The selected region cannot be captured. See runehelper.log for the reason.");

    StatusRow("Overlay");
    if (state.overlayAvailable)
        ImGui::TextColored(kGreen, "Ready");
    else
        ImGui::TextColored(kRed, "Unavailable");

    if (ImGui::IsItemHovered() && !state.overlayAvailable)
        UiTooltip("The overlay window could not be created, prices are shown in the Debug Menu only.");

    StatusRow("Prices");
    if (state.priceDownloading)
        ImGui::TextColored(kYellow, "Downloading");
    else
        ImGui::TextColored(kGreen, "%zu items loaded", state.priceCount);

    StatusRow("Version");
    DrawVersion(ui);

    ImGui::EndTable();
    ImGui::Spacing();

    return true;
}

void DrawRegionSection(UIState& state, const AppConfig& config)
{
    ImGui::SeparatorText("REGION");
    if (ImGui::Button("Select Region"))
        state.requests.selectRegion = true;

    state.regionHovered = ImGui::IsItemHovered();

    ImGui::SameLine();
    if (config.regionW > 0)
        ImGui::TextDisabled("x:%d y:%d w:%d h:%d", config.regionX, config.regionY, config.regionW, config.regionH);
    else
        ImGui::TextColored(kYellow, "No region selected");

    ImGui::Spacing();
}

bool DrawOcrSection(AppConfig& config)
{
    ImGui::SeparatorText("OCR");
    bool changed = ImGui::Checkbox("Enable OCR", &config.ocrEnabled);
    ImGui::SameLine();

    if (config.ocrEnabled)
        ImGui::TextColored(kGreen, "Running");
    else
        ImGui::TextColored(kRed, "Stopped");

    changed |= DrawGameLanguage(config);

    ImGui::Spacing();

    return changed;
}

bool DrawPriceSection(UIState& state, AppConfig& config)
{
    ImGui::SeparatorText("PRICES");

    bool changed = false;

    if (ImGui::Checkbox("Enable Price Search", &config.priceSearchEnabled))
    {
        changed = true;
        if (config.priceSearchEnabled)
            state.requests.refreshPrices = true;
    }
    if (ImGui::IsItemHovered())
        UiTooltip("Matches OCR loot text against the price cache and shows prices on the overlay.");

    if (!config.priceSearchEnabled)
        ImGui::BeginDisabled();

    if (ImGui::Button("Refresh Prices"))
        state.requests.refreshPrices = true;

    if (!config.priceSearchEnabled)
        ImGui::EndDisabled();

    ImGui::Spacing();

    return changed;
}

void DrawFeatureControls(UIManager& ui)
{
    ImGui::SeparatorText("FEATURES");

    for (const auto& feature : ui.Features().All())
        feature->DrawMainControls(ui);

    ImGui::Spacing();
}

void DrawSignature()
{
    const float spare = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeight();

    if (spare > 0.0f)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + spare);

    ImGui::Separator();

    const char* signature = "Denz";
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(signature).x);

    ImGui::TextDisabled("%s", signature);
}

void DrawMainTab(UIManager& ui)
{
    if (!DrawStatusSection(ui))
        return;

    UIState& state = ui.State();
    AppConfig& config = ui.ConfigDraft();

    DrawRegionSection(state, config);

    bool configChanged = DrawOcrSection(config);
    configChanged |= DrawPriceSection(state, config);

    DrawFeatureControls(ui);

    if (configChanged)
        ui.ApplyConfigDraft();

    DrawSignature();
}

void DrawHotkeyButton(UIManager& ui, const char* label, int& key)
{
    UIState& state = ui.State();

    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::SameLine(UiScaled(220.0f));

    const bool capturing = state.waitingForHotkey == &key;
    const std::string text = capturing ? "Press any key..." : ui.HotkeyToString(key);

    if (ImGui::Button(text.c_str(), ImVec2(UiScaled(180.0f), 0.0f)))
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

    if (ui.CaptureNextHotkey(key))
    {
        state.waitingForHotkey = nullptr;

        ui.ApplyConfigDraft();
        state.requests.registerHotkeys = true;
    }

    ImGui::PopID();
}

void DrawSettingsTab(UIManager& ui)
{
    UIState& state = ui.State();
    AppConfig& config = ui.ConfigDraft();
    bool configChanged = false;

    ImGui::SeparatorText("PRICES");

    constexpr const char* kPriceLeagues[] = { "Forbidden Rites",   "HC Forbidden Rites", "Runes of Aldur",
                                              "HC Runes of Aldur", "Standard",           "Hardcore" };

    auto pickLeague = [&config, &configChanged, &state](std::string league)
    {
        if (league.empty() || league == config.priceLeague)
            return;

        config.priceLeague = std::move(league);
        configChanged = true;

        if (config.priceSearchEnabled)
            state.requests.refreshPrices = true;
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
                ImGuiInputTextFlags_EnterReturnsTrue
            ))
        {
            pickLeague(state.customLeague);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndCombo();
    }

    if (leagueHovered)
        UiTooltip("A league missing from the list can be typed in. New leagues work as soon as the price proxy carries them.");

    constexpr const char* kPriceUnits[] = { "Exalted", "Exalted + divine", "Divine" };

    int priceUnit = static_cast<int>(config.priceUnit);

    if (ImGui::Combo("Units", &priceUnit, kPriceUnits, IM_ARRAYSIZE(kPriceUnits)))
    {
        config.priceUnit = static_cast<PriceUnit>(priceUnit);
        configChanged = true;
    }

    if (ImGui::IsItemHovered())
        UiTooltip("Exalted + divine adds the divine value to rows worth at least one divine. Divine shows "
                  "every price in divine orbs.");

    ImGui::Spacing();

    configChanged |= ImGui::Checkbox("Automatic colors", &config.autoPriceColors);

    if (ImGui::IsItemHovered())
        UiTooltip("Colors each price against the most valuable row on screen: red from half its value, yellow from a fifth, "
                  "green from a twentieth. Turn it off to set the thresholds below yourself.");

    ImGui::BeginDisabled(config.autoPriceColors);
    configChanged |= ImGui::InputInt("Green >= ex", &config.priceColorMedium);
    configChanged |= ImGui::InputInt("Yellow >= ex", &config.priceColorHigh);
    configChanged |= ImGui::InputInt("Red >= ex", &config.priceColorVeryHigh);
    ImGui::EndDisabled();
    configChanged |=
        ImGui::SliderInt("Refresh minutes", &config.priceRefreshMinutes, kMinPriceRefreshMinutes, kMaxPriceRefreshMinutes);

    ImGui::Spacing();

    ImGui::SeparatorText("OVERLAY");

    configChanged |= ImGui::SliderInt("Offset X", &config.overlayOffsetX, -300, 500);
    configChanged |= ImGui::SliderInt("Offset Y", &config.overlayOffsetY, -200, 200);
    configChanged |= ImGui::SliderInt("Font Size", &config.overlayFontSize, kMinOverlayFontSize, kMaxOverlayFontSize);

    configChanged |= ImGui::Checkbox("Background", &config.overlayBackground);

    if (ImGui::IsItemHovered())
        UiTooltip("Draws a dark plate behind the overlay text. Turn it off for bare text over the game.");

    configChanged |= ImGui::Checkbox("Outline", &config.overlayOutline);

    if (ImGui::IsItemHovered())
        UiTooltip("Traces the text in black so it stays readable without a background plate.");

    configChanged |= ImGui::Checkbox("Currency icons", &config.overlayIcons);

    if (ImGui::IsItemHovered())
        UiTooltip("Shows the Exalted and Divine Orb pictures instead of ex and div.");

    ImGui::Spacing();

    ImGui::SeparatorText("HOTKEYS");

    DrawHotkeyButton(ui, "Toggle OCR", config.hotkeyToggleOCR);
    DrawHotkeyButton(ui, "Single Snapshot", config.hotkeySingleSnapshot);
    DrawHotkeyButton(ui, "Select Region", config.hotkeySelectRegion);

    if (configChanged)
        ui.ApplyConfigDraft();
}

void DrawReportStatus(const UIState& state)
{
    switch (state.report)
    {
    case ReportState::None: return;
    case ReportState::Collecting: ImGui::TextColored(kYellow, "Collecting the report..."); return;
    case ReportState::Failed: ImGui::TextColored(kRed, "The report could not be written, see runehelper.log"); return;
    case ReportState::Saved: break;
    }

    ImGui::TextColored(kGreen, "Report saved");
    ImGui::SameLine();

    if (ImGui::SmallButton("Open Folder"))
    {
        if (!OpenExternalUrl(state.reportFolder))
            LOG_ERROR("Could not open the report folder");
    }

    if (ImGui::IsItemHovered())
        UiTooltip(state.reportFolder.c_str());

    ImGui::SameLine();

    if (ImGui::SmallButton("New GitHub Issue"))
    {
        if (!OpenExternalUrl(kNewIssueUrl))
            LOG_ERROR("Could not open the GitHub issue page in a browser");
    }

    if (ImGui::IsItemHovered())
        UiTooltip("Opens a new issue on GitHub. Drag the report zip into it.");
}

void DrawPauseSwitch(UIManager& ui)
{
    AppConfig& config = ui.ConfigDraft();
    const bool supported = GameFocusSupported();
    bool unavailable = false;

    ImGui::BeginDisabled(!supported);

    if (ImGui::Checkbox("Pause while Path of Exile is not active", supported ? &config.pauseWhenGameInactive : &unavailable))
        ui.ApplyConfigDraft();

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        UiTooltip(
            supported ? "Stops reading the screen and hides the overlay while another window is in front of the game."
                      : "Unavailable for Wayland"
        );
}

void DrawDebugTab(UIManager& ui)
{
    UIState& state = ui.State();

    DrawPauseSwitch(ui);

    if (ImGui::Button("Save OCR Debug"))
        state.requests.saveOcrDebug = true;

    if (ImGui::IsItemHovered())
        UiTooltip("Reads the region once and writes its crops and recognition logs into the ocr_debug/latest folder.");

    ImGui::SameLine();
    ImGui::BeginDisabled(state.report == ReportState::Collecting);

    if (ImGui::Button("Create Bug Report"))
        state.requests.createReport = true;

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        UiTooltip("Reads the region once, like Save OCR Debug, then packs the crops, the log, the settings and system details "
                  "into a zip to attach to a GitHub issue. Keep the menu open in the game while you press it.");

    DrawReportStatus(state);

    ImGui::Spacing();

    ImGui::SeparatorText("OCR DEBUG");

    const DebugData& debug = ui.GetDebugData();

    if (debug.lines.empty())
    {
        ImGui::TextDisabled("No OCR data yet.");
        return;
    }

    const float available = ImGui::GetContentRegionAvail().y;
    const float minimumHeight = UiScaled(kMinDebugTableHeight);
    const ImVec2 tableSize(0.0f, available > minimumHeight ? available : minimumHeight);

    if (!ImGui::BeginTable(
            "ocr_debug_table",
            4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
            tableSize
        ))
    {
        return;
    }

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
        UIDraw::CellText(line.ocrText.c_str());

        const bool matched = line.Matched();

        ImGui::TableSetColumnIndex(1);

        if (matched)
            UIDraw::CellText(line.matchedText.c_str());
        else
            ImGui::TextDisabled("not in price list");

        ImGui::TableSetColumnIndex(2);

        if (!matched)
        {
            ImGui::TextDisabled("-");

            if (ImGui::IsItemHovered())
                UiTooltip("This item has no price on the market, so RuneHelper has nothing to show.");
        }
        else if (line.confidence >= kTrustedMatchConfidence)
        {
            ImGui::TextColored(kGreen, "%d%%", line.confidence);
        }
        else
        {
            ImGui::TextColored(kYellow, "%d%% ?", line.confidence);

            if (ImGui::IsItemHovered())
                UiTooltip("The name was guessed, not read cleanly. Verify before trusting this price.");
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

void UIDraw::CellText(const char* text)
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

void UIDraw::Draw(UIManager& ui)
{
    UIState& state = ui.State();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    ImGui::Begin(
        "RuneHelper",
        nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    DrawTitleBar(ui);

    state.debugTabOpen = false;
    state.featureTabOpen = false;

    if (ImGui::BeginTabBar("MainTabs"))
    {
        if (ImGui::BeginTabItem("RuneHelper"))
        {
            DrawMainTab(ui);
            ImGui::EndTabItem();
        }

        for (const auto& feature : ui.Features().All())
        {
            const char* title = feature->TabTitle();

            if (!title)
                continue;

            if (ImGui::BeginTabItem(title))
            {
                state.featureTabOpen = true;
                feature->DrawTab(ui);
                ImGui::EndTabItem();
            }
        }

        if (ImGui::BeginTabItem("Settings"))
        {
            DrawSettingsTab(ui);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Debug Menu"))
        {
            state.debugTabOpen = true;
            DrawDebugTab(ui);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
