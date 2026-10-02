#include "ui/UIDraw.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "core/Feature.h"
#include "common/Logger.h"
#include "core/UpdateChecker.h"
#include "platform/GameFocus.h"
#include "platform/PlatformShell.h"
#include "price/ResolvedPrice.h"
#include "ui/UIManager.h"
#include "ui/UiScale.h"
#include "ui/UiWidgets.h"
#include "ui/OverlayIcons.h"
#include "ui/UiTooltip.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

namespace
{
constexpr ImVec4 kGreen{ 0.56f, 0.80f, 0.65f, 1.0f };
constexpr ImVec4 kYellow{ 0.80f, 0.71f, 0.54f, 1.0f };
constexpr ImVec4 kRed{ 0.89f, 0.60f, 0.60f, 1.0f };
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

    UiWidgets::Field("Game language");
    if (ImGui::BeginCombo("##game_language", current != languages.end() ? current->name.data() : config.gameLanguage.c_str()))
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
    const float titleBarHeight = UiScaled(24.0f);
    const ImVec2 titleButton(UiScaled(24.0f), UiScaled(24.0f));

    ImGui::BeginChild("TitleBar", ImVec2(0, titleBarHeight), false);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("RuneHelper");
    ImGui::SameLine();
    ImGui::TextDisabled("v%s", RUNEHELPER_VERSION);

#ifdef _WIN32
    ImGui::SameLine(ImGui::GetWindowWidth() - UiScaled(88.0f));
#else
    ImGui::SameLine(ImGui::GetWindowWidth() - UiScaled(56.0f));
#endif
    state.titleButtonsLeft = ImGui::GetCursorScreenPos().x;

#ifdef _WIN32
    if (ImGui::Button("##Tray", titleButton))
        ui.MinimizeToTray();

    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const ImVec2 center((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f);
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddTriangleFilled(
        ImVec2(center.x - UiScaled(4.0f), center.y - UiScaled(4.0f)),
        ImVec2(center.x + UiScaled(4.0f), center.y - UiScaled(4.0f)),
        ImVec2(center.x, center.y + UiScaled(1.0f)),
        color
    );
    draw->AddLine(
        ImVec2(center.x - UiScaled(4.0f), center.y + UiScaled(4.0f)),
        ImVec2(center.x + UiScaled(4.0f), center.y + UiScaled(4.0f)),
        color,
        UiScaled(1.0f)
    );

    if (ImGui::IsItemHovered())
        UiTooltip("Minimize to tray");

    ImGui::SameLine();
#endif

    if (ImGui::Button("_", titleButton))
        ui.Minimize();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
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
            UiTooltip("Path of Exile is not the active window. This pause can be turned off in Settings > General.");

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
        ui.EnqueueCommand(UICommand::InstallUpdate);

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

void DrawPriceStatus(const PriceStatus& status)
{
    if (status.downloading)
        ImGui::TextColored(kYellow, "Downloading (%zu items loaded)", status.priceCount);
    else if (status.priceCount == 0)
        ImGui::TextColored(kYellow, "No data for selected league");
    else if (status.refreshFailed)
        ImGui::TextColored(kYellow, "Using cached prices (%zu items)", status.priceCount);
    else
        ImGui::TextColored(kGreen, "%zu items loaded", status.priceCount);

    if (ImGui::IsItemHovered() && status.refreshFailed)
        UiTooltip("The last download failed or was incomplete. Available prices are kept. See runehelper.log for details.");

    if (status.priceCount == 0)
        return;

    char updated[32]{};
    const std::time_t timestamp = static_cast<std::time_t>(status.updatedAt);
    std::tm local{};
#ifdef _WIN32
    const bool converted = localtime_s(&local, &timestamp) == 0;
#else
    const bool converted = localtime_r(&timestamp, &local) != nullptr;
#endif
    if (status.updatedAt > 0 && converted && std::strftime(updated, sizeof(updated), "%Y-%m-%d %H:%M", &local) > 0)
        ImGui::TextDisabled("Updated: %s", updated);
    else
        ImGui::TextDisabled("Updated: unknown");

    if (ImGui::IsItemHovered())
        UiTooltip("Last complete price update, in your local time.");
}

void DrawStatusSection(UIManager& ui)
{
    const UIState& state = ui.State();

    ImGui::SeparatorText("STATUS");

    if (!ImGui::BeginTable("status_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return;

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
        UiTooltip("The overlay window could not be created, prices are shown in Diagnostics only.");

    StatusRow("Prices");
    DrawPriceStatus(state.prices);

    StatusRow("Version");
    DrawVersion(ui);

    ImGui::EndTable();
    ImGui::Spacing();
}

void DrawFeatureTools(UIManager& ui, const char* name)
{
    for (const auto& feature : ui.Features().All())
    {
        if (feature->Name() == name)
            feature->DrawTools(ui);
    }
}

void DrawFeatureSettings(UIManager& ui, const char* name)
{
    for (const auto& feature : ui.Features().All())
    {
        if (feature->Name() == name)
            feature->DrawSettings(ui);
    }
}

bool DrawToolHeader(const char* title, UiWidgets::Icon icon, bool& enabled)
{
    UiWidgets::DrawIcon(icon);
    ImGui::SameLine();
    return UiWidgets::Toggle(title, enabled);
}

void DrawTools(UIManager& ui)
{
    if (ui.Updates().HasUpdate())
    {
        ImGui::TextColored(kYellow, "Update available");
        ImGui::SameLine();
        DrawUpdate(ui);
        UiWidgets::Divider();
    }

    AppConfig& config = ui.ConfigDraft();
    const UIState& state = ui.State();
    bool changed = DrawToolHeader("RuneShape", UiWidgets::Icon::RuneShape, config.ocrEnabled);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    UIDraw::CellText("Prices for items in the RuneShape loot window.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    UiWidgets::Field("Capture area");
    if (ImGui::Button(config.regionW > 0 ? "Change area" : "Select area", ImVec2(UiScaled(170), 0)))
        ui.EnqueueCommand(UICommand::SelectRegion);
    ui.State().regionHovered = ImGui::IsItemHovered();
    changed |= DrawGameLanguage(config);
    ImGui::Spacing();

    if (!config.ocrEnabled)
        ImGui::TextDisabled("Screen reading is paused.");
    else if (state.ocr.state == OcrState::Failed)
        ImGui::TextColored(kRed, "Recognition unavailable. See Diagnostics.");
    else if (state.ocr.captureFailing)
        ImGui::TextColored(kRed, "Capture failed. Select the area again.");
    else if (!state.overlayAvailable)
        ImGui::TextColored(kRed, "Overlay unavailable. See Diagnostics.");
    else if (config.regionW <= 0 || config.regionH <= 0)
        ImGui::TextColored(kYellow, "Select an area to start reading.");
    else if (state.ocr.state == OcrState::Initializing)
        ImGui::TextDisabled("Preparing recognition...");
    else if (state.ocr.waitingForGame)
        ImGui::TextDisabled("Area selected. Waiting for the game.");
    else
        ImGui::TextDisabled("Area selected. Ready to read RuneShape.");

    ImGui::Spacing();
    DrawFeatureTools(ui, "expedition");
    UiWidgets::Divider();

    changed |= DrawToolHeader("Maps & Tablets", UiWidgets::Icon::Map, config.showMapsTab);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    UIDraw::CellText("Copy a Waystone or Tablet with Ctrl+C\nto check its modifiers against your filters.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    DrawFeatureTools(ui, "map_check");
    UiWidgets::Divider();

    changed |= DrawToolHeader("Item Price Lookup", UiWidgets::Icon::Currency, config.currencyClipboardPriceEnabled);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    UIDraw::CellText("Copy a supported item with Ctrl+C\nto see its market price beside the cursor.");
    ImGui::PopStyleColor();

    if (changed)
        ui.ApplyConfigDraft();
}

void DrawHotkeyButton(UIManager& ui, const char* label, int& key)
{
    UIState& state = ui.State();

    ImGui::PushID(label);

    UiWidgets::Field(label, 150.0f);

    const bool capturing = state.waitingForHotkey == &key;
    const std::string text = capturing ? "Press any key..." : ui.HotkeyToString(key);

    if (ImGui::Button(text.c_str(), ImVec2(UiScaled(150.0f), 0.0f)))
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
        ui.EnqueueCommand(UICommand::RegisterHotkeys);
    }

    ImGui::PopID();
}

void DrawPauseSwitch(UIManager& ui);

bool DrawLeague(UIManager& ui)
{
    UIState& state = ui.State();
    AppConfig& config = ui.ConfigDraft();
    bool changed = false;
    constexpr const char* leagues[] = { "Forbidden Rites",   "HC Forbidden Rites", "Runes of Aldur",
                                        "HC Runes of Aldur", "Standard",           "Hardcore" };
    const auto pick = [&config, &changed, &ui](std::string league)
    {
        if (league.empty() || league == config.priceLeague)
            return;

        config.priceLeague = std::move(league);
        changed = true;
        if ((config.ocrEnabled && config.priceSearchEnabled) || config.currencyClipboardPriceEnabled)
            ui.EnqueueCommand(UICommand::RefreshPrices);
    };

    UiWidgets::Field("League");
    if (ImGui::BeginCombo("##league", config.priceLeague.c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (const char* league : leagues)
        {
            const bool selected = config.priceLeague == league;
            if (ImGui::Selectable(league, selected))
                pick(league);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();
        if (ImGui::IsWindowAppearing())
            std::snprintf(state.customLeague, sizeof(state.customLeague), "%s", config.priceLeague.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint(
                "##custom_league",
                "Another league, then Enter",
                state.customLeague,
                sizeof(state.customLeague),
                ImGuiInputTextFlags_EnterReturnsTrue
            ))
        {
            pick(state.customLeague);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawGeneralSettings(UIManager& ui)
{
    AppConfig& config = ui.ConfigDraft();
    UiWidgets::Section("MARKET DATA");
    bool changed = DrawLeague(ui);
    UiWidgets::Field("Refresh interval");
    const std::string interval = std::to_string(config.priceRefreshMinutes) + " minutes";
    if (ImGui::BeginCombo("##refresh", interval.c_str()))
    {
        for (const int minutes : { 5, 15, 30, 60, 120, 360 })
        {
            const bool selected = config.priceRefreshMinutes == minutes;
            const std::string label = std::to_string(minutes) + " minutes";
            if (ImGui::Selectable(label.c_str(), selected))
            {
                config.priceRefreshMinutes = minutes;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Prices update automatically for enabled tools.");
    UiWidgets::Divider();
    UiWidgets::Section("GAME FOCUS");
    DrawPauseSwitch(ui);
    UiWidgets::Divider();
    UiWidgets::Section("RUNESHAPE PRICES");
    changed |= ImGui::Checkbox("Show prices in RuneShape", &config.priceSearchEnabled);
    constexpr const char* units[] = { "Exalted", "Exalted + Divine", "Divine" };
    int unit = static_cast<int>(config.priceUnit);
    UiWidgets::Field("Units");
    if (ImGui::Combo("##price_units", &unit, units, IM_ARRAYSIZE(units)))
    {
        config.priceUnit = static_cast<PriceUnit>(unit);
        changed = true;
    }
    changed |= ImGui::Checkbox("Automatic price colors", &config.autoPriceColors);
    if (ImGui::IsItemHovered())
        UiTooltip("Colors prices relative to the most valuable row. Disable to choose thresholds in Exalted Orbs.");
    if (!config.autoPriceColors)
    {
        UiWidgets::Field("Green from");
        changed |= ImGui::InputInt("##green", &config.priceColorMedium);
        UiWidgets::Field("Yellow from");
        changed |= ImGui::InputInt("##yellow", &config.priceColorHigh);
        UiWidgets::Field("Red from");
        changed |= ImGui::InputInt("##red", &config.priceColorVeryHigh);
    }
    return changed;
}

void DrawAppearancePreview(UIManager& ui, int selected, int fontSize, bool background, bool outline)
{
    UiWidgets::Divider();
    UiWidgets::Section("PREVIEW");
    OverlayPanel panel;
    panel.width = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x));
    panel.fontSize = static_cast<int>(UiScaled(static_cast<float>(fontSize)));
    panel.background = background;
    panel.outline = outline;
    const std::string exalted = selected == 0 && !ui.ConfigDraft().overlayIcons ? "ex" : IconString(kExaltedOrbIcon);
    const std::string divine = selected == 0 && !ui.ConfigDraft().overlayIcons ? "div" : IconString(kDivineOrbIcon);
    if (selected == 1)
        panel.lines = { { "Bleak Crosscut - T15", OverlayRgb(214, 189, 133) },
                        { "Avoid: elemental penetration", OverlayRgb(226, 154, 152) },
                        { "Monsters penetrate 14% resistance", OverlayRgb(167, 164, 218) } };
    else if (selected == 2)
        panel.lines = { { "Vaal Orb", OverlayRgb(214, 189, 133) }, { "12.5 " + exalted + "    0.02 " + divine } };
    else
    {
        const bool inDivine = ui.ConfigDraft().priceUnit == PriceUnit::Divine;
        panel.lines = { { "Vaal Orb    " + (inDivine ? "0.02 " + divine : "12.5 " + exalted), OverlayRgb(143, 204, 167) },
                        { "Chaos Orb   " + (inDivine ? "0.01 " + divine : "4.2 " + exalted), OverlayRgb(226, 216, 150) } };
    }
    panel.height = static_cast<int>(UiScaled(150));
    const OverlayPreview preview = ui.PreviewImage(panel, selected == 0);
    panel.height = preview.height;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + panel.width, origin.y + panel.height), IM_COL32(23, 25, 28, 255), UiScaled(4));
    const float step = UiScaled(24);
    for (int column = 0; column * step < panel.width; ++column)
    {
        const float x = origin.x + column * step;
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + panel.height), IM_COL32(255, 255, 255, 7));
    }
    for (int row = 0; row * step < panel.height; ++row)
    {
        const float y = origin.y + row * step;
        draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + panel.width, y), IM_COL32(255, 255, 255, 7));
    }
    if (preview.texture)
        ImGui::Image(
            reinterpret_cast<ImTextureID>(preview.texture),
            ImVec2(static_cast<float>(panel.width), static_cast<float>(panel.height))
        );
    else
        ImGui::Dummy(ImVec2(static_cast<float>(panel.width), static_cast<float>(panel.height)));
    draw->AddRect(origin, ImVec2(origin.x + panel.width, origin.y + panel.height), ImGui::GetColorU32(ImGuiCol_Border), UiScaled(4));
    ImGui::TextDisabled("Example item and prices. Changes appear immediately.");
}

bool DrawAppearanceSettings(UIManager& ui)
{
    AppConfig& config = ui.ConfigDraft();
    int& selected = ui.State().appearanceSection;
    constexpr const char* labels[] = { "RuneShape", "Maps", "Item Prices" };
    const float width = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i)
    {
        if (i)
            ImGui::SameLine();
        if (UiWidgets::Tab(labels[i], selected == i, width))
            selected = i;
    }
    ImGui::Spacing();
    int& fontSize = selected == 0 ? config.overlayFontSize : selected == 1 ? config.mapsFontSize : config.currencyPriceFontSize;
    bool& background = selected == 0   ? config.overlayBackground
                       : selected == 1 ? config.mapsPanelBackground
                                       : config.pricePanelBackground;
    bool& outline = selected == 0 ? config.overlayOutline : selected == 1 ? config.mapsPanelOutline : config.pricePanelOutline;
    ImGui::PushID(selected);
    UiWidgets::Field("Font size");
    bool changed = ImGui::SliderInt("##font_size", &fontSize, kMinOverlayFontSize, kMaxOverlayFontSize, "%d px");
    changed |= ImGui::Checkbox("Background", &background);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Text outline", &outline);
    if (selected == 0)
        changed |= ImGui::Checkbox("Currency icons", &config.overlayIcons);
    UiWidgets::Section("POSITION");
    if (selected == 0)
    {
        UiWidgets::Field("Offset X");
        changed |= ImGui::SliderInt("##offset_x", &config.overlayOffsetX, -300, 500, "%d px");
        UiWidgets::Field("Offset Y");
        changed |= ImGui::SliderInt("##offset_y", &config.overlayOffsetY, -200, 200, "%d px");
    }
    else if (selected == 1)
        DrawFeatureSettings(ui, "map_check");
    else
        UIDraw::CellText("Above and to the right of the cursor, kept inside the screen.");
    DrawAppearancePreview(ui, selected, fontSize, background, outline);
    if (selected == 0)
    {
        UiWidgets::Divider();
        UiWidgets::Section("EXPEDITION");
        DrawFeatureSettings(ui, "expedition");
    }
    ImGui::PopID();
    return changed;
}

void DrawSettings(UIManager& ui)
{
    int& selected = ui.State().settingsSection;
    constexpr const char* labels[] = { "General", "Appearance", "Hotkeys" };
    const float width = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i)
    {
        if (i)
            ImGui::SameLine();
        if (UiWidgets::Tab(labels[i], selected == i, width))
        {
            selected = i;
            ui.State().waitingForHotkey = nullptr;
        }
    }
    ImGui::Spacing();
    ImGui::PushID(selected);
    bool changed = false;
    if (selected == 0)
        changed = DrawGeneralSettings(ui);
    else if (selected == 1)
        changed = DrawAppearanceSettings(ui);
    else
    {
        UiWidgets::Section("KEYBOARD SHORTCUTS");
        AppConfig& config = ui.ConfigDraft();
        DrawHotkeyButton(ui, "Toggle RuneShape", config.hotkeyToggleOCR);
        DrawHotkeyButton(ui, "Read region once", config.hotkeySingleSnapshot);
        DrawHotkeyButton(ui, "Select capture area", config.hotkeySelectRegion);
        UiWidgets::Divider();
        UiWidgets::Section("COPIED ITEMS");
        const float keycapWidth = ImGui::CalcTextSize("Ctrl+C").x / UiScaled(1) + 12;
        UiWidgets::Field("Maps & Item Price Lookup", keycapWidth);
        UiWidgets::Keycap("Ctrl+C");
        ImGui::TextDisabled("Uses the game's copy command.");
    }
    ImGui::PopID();
    if (changed)
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

void DrawOcrDebugData(const DebugData& debug)
{
    ImGui::SeparatorText("OCR DEBUG");

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

void DrawDebugTab(UIManager& ui)
{
    UIState& state = ui.State();

    DrawStatusSection(ui);

    if (ImGui::Button("Save OCR Debug"))
        ui.EnqueueCommand(UICommand::SaveOcrDebug);

    if (ImGui::IsItemHovered())
        UiTooltip("Reads the region once and writes its crops and recognition logs into the ocr_debug/latest folder.");

    ImGui::SameLine();
    ImGui::BeginDisabled(state.report == ReportState::Collecting);

    if (ImGui::Button("Create Bug Report"))
        ui.EnqueueCommand(UICommand::CreateReport);

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        UiTooltip("Reads the region once, like Save OCR Debug, then packs the crops, the log, the settings and system details "
                  "into a zip to attach to a GitHub issue. Keep the menu open in the game while you press it.");

    DrawReportStatus(state);

    ImGui::Spacing();
    ImGui::SeparatorText("IMGUI DEBUG");
    ImGui::Checkbox("Metrics / Debugger", &state.showImGuiMetrics);
    ImGui::SameLine();
    ImGui::Checkbox("Debug Log", &state.showImGuiDebugLog);

    if (ImGui::IsItemHovered())
        UiTooltip("Shows Dear ImGui input, focus and popup events in a separate window.");

    ImGui::Spacing();

    if (!ImGui::BeginTabBar("DebugSections"))
        return;

    if (ImGui::BeginTabItem("OCR"))
    {
        DrawOcrDebugData(ui.GetDebugData());
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Expedition"))
    {
        for (const auto& feature : ui.Features().All())
        {
            if (feature->Name() == "expedition")
                feature->DrawDebug(ui);
        }

        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Item Prices"))
    {
        for (const auto& feature : ui.Features().All())
        {
            if (feature->Name() == "currency_price")
                feature->DrawDebug(ui);
        }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

void DrawNavigation(UIManager& ui)
{
    UIState& state = ui.State();
    constexpr const char* labels[] = { "Tools", "Maps", "Settings" };
    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i)
    {
        if (i)
            ImGui::SameLine();
        const auto page = static_cast<UIPage>(i);
        if (UiWidgets::Tab(labels[i], state.page == page, UiScaled(82)))
        {
            state.page = page;
            state.waitingForHotkey = nullptr;
        }
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - UiScaled(32));
    if (UiWidgets::Tab("...", state.page == UIPage::Diagnostics || state.page == UIPage::About, UiScaled(32)))
        ImGui::OpenPopup("More");
    if (ImGui::BeginPopup("More"))
    {
        if (ImGui::MenuItem("Diagnostics"))
        {
            state.page = UIPage::Diagnostics;
            state.waitingForHotkey = nullptr;
        }
        if (ImGui::MenuItem("About RuneHelper"))
        {
            state.page = UIPage::About;
            state.waitingForHotkey = nullptr;
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();
}

void DrawFooter(UIManager& ui)
{
    const AppConfig& config = ui.ConfigDraft();
    const PriceStatus& prices = ui.State().prices;
    std::string status;
    const bool active = (config.ocrEnabled && config.priceSearchEnabled) || config.currencyClipboardPriceEnabled;
    if (prices.downloading)
        status = "Updating prices...";
    else if (!active)
        status = "Updates paused";
    else if (prices.refreshFailed)
        status = prices.priceCount ? "Using cached prices" : "Prices unavailable";
    else if (prices.priceCount == 0)
        status = "No prices for this league";
    else if (prices.updatedAt > 0)
    {
        const auto age = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::time(nullptr)) - prices.updatedAt);
        status = age < 60 ? "Prices just updated" : "Updated " + std::to_string(age / 60) + " min ago";
    }
    else
        status = "Cached prices loaded";

    ImGui::Separator();
    if (ImGui::BeginTable("Footer", 3, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("League", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(status.c_str()).x);
        ImGui::TableSetupColumn("Refresh", ImGuiTableColumnFlags_WidthFixed, UiScaled(58));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", config.priceLeague.c_str());
        if (ImGui::IsItemHovered())
            UiTooltip(config.priceLeague.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(prices.refreshFailed ? kYellow : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", status.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(prices.downloading || !active);
        if (ImGui::SmallButton("Refresh"))
            ui.EnqueueCommand(UICommand::RefreshPrices);
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
}

void DrawAbout(UIManager& ui)
{
    UiWidgets::Section("RUNEHELPER");
    DrawVersion(ui);
    ImGui::Spacing();
    UIDraw::CellText("Screen reading, map filters and item prices for Path of Exile 2.");
    UiWidgets::Divider();
    if (ImGui::Button("Project page"))
        OpenExternalUrl("https://github.com/Denzeriko/RuneHelper");
    ImGui::SameLine();
    if (ImGui::Button("Report an issue"))
        OpenExternalUrl(kNewIssueUrl);
    ImGui::Spacing();
    ImGui::TextDisabled("Denz");
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
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (state.showImGuiMetrics || state.showImGuiDebugLog)
        flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("RuneHelper", nullptr, flags);
    DrawTitleBar(ui);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, UiScaled(4)));
    DrawNavigation(ui);
    ImGui::PopStyleVar();
    state.regionHovered = false;
    constexpr const char* pages[] = { "ToolsContent", "MapsContent", "SettingsContent", "DiagnosticsContent", "AboutContent" };
    const float contentWidth = ImGui::GetContentRegionAvail().x - UiScaled(16) - ImGui::GetStyle().ScrollbarSize;
    ImGui::SetNextWindowContentSize(ImVec2(std::max(1.0f, contentWidth), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UiScaled(8), UiScaled(6)));
    const bool visible =
        ImGui::BeginChild(pages[static_cast<int>(state.page)], ImVec2(0, -UiScaled(42)), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    if (visible)
    {
        switch (state.page)
        {
        case UIPage::Tools: DrawTools(ui); break;
        case UIPage::Maps:
            for (const auto& feature : ui.Features().All())
            {
                if (feature->Name() == "map_check")
                    feature->DrawTab(ui);
            }
            break;
        case UIPage::Settings: DrawSettings(ui); break;
        case UIPage::Diagnostics: DrawDebugTab(ui); break;
        case UIPage::About: DrawAbout(ui); break;
        }
    }
    ImGui::EndChild();
    DrawFooter(ui);
    ImGui::End();
    if (state.showImGuiMetrics)
        ImGui::ShowMetricsWindow(&state.showImGuiMetrics);
    if (state.showImGuiDebugLog)
        ImGui::ShowDebugLogWindow(&state.showImGuiDebugLog);
}
