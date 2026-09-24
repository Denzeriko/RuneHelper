#include "features/ExpeditionFeature.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/ConfigManager.h"
#include "core/JsonRead.h"
#include "core/Logger.h"
#include "ocr/LootParser.h"
#include "ui/UIDraw.h"
#include "ui/UIManager.h"
#include "ui/UiScale.h"
#include "ui/UiTooltip.h"

namespace
{
constexpr ImVec4 kGreen{ 0.5f, 1.0f, 0.5f, 1.0f };
constexpr ImVec4 kYellow{ 1.0f, 0.8f, 0.2f, 1.0f };

constexpr double kPerWaveTolerance = 0.01;

constexpr int kRowSnap = 6;

constexpr float kMinTableHeight = 120.0f;

bool AtomicCheckbox(const char* label, std::atomic<bool>& value)
{
    bool current = value;

    if (!ImGui::Checkbox(label, &current))
        return false;

    value = current;
    return true;
}

std::string FormatPerWave(double value)
{
    char buffer[32];

    if (value >= 100.0)
        std::snprintf(buffer, sizeof(buffer), "%.0f/w", value);
    else if (value >= 10.0)
        std::snprintf(buffer, sizeof(buffer), "%.1f/w", value);
    else
        std::snprintf(buffer, sizeof(buffer), "%.2f/w", value);

    return buffer;
}

struct ScreenRecipe
{
    const Recipe* recipe = nullptr;
    size_t rowIndex = 0;
    double perWave = 0.0;
};
}

bool ExpeditionFeature::Init(ConfigManager& configManager)
{
    configManager_ = &configManager;

    const nlohmann::json settings = configManager.FeatureSettings(Name());

    settings_.enabled = JsonValue(settings, "enabled", settings_.enabled.load());
    settings_.showRunes = JsonValue(settings, "showRunes", settings_.showRunes.load());
    settings_.highlightRare = JsonValue(settings, "highlightRare", settings_.highlightRare.load());

    const bool loaded = database_.Load();

    if (!loaded)
        LOG_ERROR("Expedition feature is disabled: the combination database could not be loaded");

    updater_.Start();

    return loaded;
}

std::string ExpeditionFeature::DataStatus() const
{
    if (!database_.Loaded())
        return "no combinations.json loaded";

    std::string status = std::to_string(database_.Recipes().size()) + " combos";

    if (!database_.Complete())
        status += " (partial data - run tools/scrape_poe2db.py)";

    return status;
}

void ExpeditionFeature::Shutdown()
{
    updater_.Stop();
}

void ExpeditionFeature::OnRegionChanged()
{
    regionDirty_ = true;
}

void ExpeditionFeature::RebuildTabRows(const DebugData& debug)
{
    tabRows_.clear();
    tabPlaced_.clear();

    for (const auto& line : debug.lines)
    {
        const auto parsed = LootParser::ParseLootLine(line.ocrText);
        const std::string& name = line.matchedText != "-" ? line.matchedText : parsed.itemName;

        const Recipe* recipe = database_.FindRecipe(name, parsed.quantity);

        if (!recipe && name != parsed.itemName)
            recipe = database_.FindRecipe(parsed.itemName, parsed.quantity);

        if (!recipe || recipe->runes.empty())
            continue;

        double perWave = 0.0;

        if (line.priceEx > 0.0)
            perWave = line.priceEx * parsed.quantity / static_cast<double>(recipe->runes.size());

        tabRows_.push_back({ recipe, perWave });
    }

    if (tabRows_.empty())
        return;

    tabPlaced_ = tabRows_.front().recipe->runes;

    for (const auto& entry : tabRows_)
    {
        size_t common = 0;

        while (common < tabPlaced_.size() && common < entry.recipe->runes.size() && tabPlaced_[common] == entry.recipe->runes[common])
        {
            ++common;
        }

        tabPlaced_.resize(common);
    }

    std::sort(
        tabRows_.begin(),
        tabRows_.end(),
        [](const ExpeditionTabRow& a, const ExpeditionTabRow& b)
        {
            if (a.perWave != b.perWave)
                return a.perWave > b.perWave;

            return a.recipe->runes.size() < b.recipe->runes.size();
        }
    );
}

void ExpeditionFeature::SaveSettings()
{
    if (!configManager_)
        return;

    nlohmann::json settings;
    settings["enabled"] = settings_.enabled.load();
    settings["showRunes"] = settings_.showRunes.load();
    settings["highlightRare"] = settings_.highlightRare.load();

    configManager_->SetFeatureSettings(Name(), std::move(settings));

    if (!configManager_->Save())
        LOG_ERROR("Expedition feature failed to save settings");
}

void ExpeditionFeature::OnFrame(FrameContext& frame)
{
    if (regionDirty_.exchange(false))
    {
        tiles_ = RuneTileLocator{};
        markSignature_.clear();
        cachedMarks_.clear();
    }

    if (!database_.Loaded())
        return;

    if (!settings_.enabled && !settings_.highlightRare)
        return;

    std::vector<ScreenRecipe> found;

    for (size_t i = 0; i < frame.rows.size(); ++i)
    {
        const FrameRow& row = frame.rows[i];
        const ResolvedPrice& resolved = row.price;
        const std::string& matchedName = resolved.name.empty() ? row.name : resolved.name;

        const Recipe* recipe = database_.FindRecipe(matchedName, row.quantity);

        if (!recipe && matchedName != row.name)
            recipe = database_.FindRecipe(row.name, row.quantity);

        if (!recipe || recipe->runes.empty())
            continue;

        ScreenRecipe entry;
        entry.recipe = recipe;
        entry.rowIndex = i;

        if (resolved.totalEx > 0.0)
            entry.perWave = resolved.totalEx / static_cast<double>(recipe->runes.size());

        found.push_back(entry);
    }

    if (found.empty())
        return;

    if (settings_.enabled)
    {
        auto best = found.end();

        for (auto it = found.begin(); it != found.end(); ++it)
        {
            if (it->perWave <= 0.0)
                continue;

            if (best == found.end())
            {
                best = it;
                continue;
            }

            const double relative = std::abs(it->perWave - best->perWave) / best->perWave;

            if (relative <= kPerWaveTolerance)
            {
                if (it->recipe->runes.size() < best->recipe->runes.size())
                    best = it;
            }
            else if (it->perWave > best->perWave)
            {
                best = it;
            }
        }

        for (auto it = found.begin(); it != found.end(); ++it)
        {
            RowOverlay& overlay = frame.rowOverlays[it->rowIndex];

            std::string note = std::to_string(it->recipe->runes.size()) + "w";

            if (it->perWave > 0.0)
                note += " " + FormatPerWave(it->perWave);

            if (settings_.showRunes)
            {
                note += "  ";

                for (size_t i = 0; i < it->recipe->runes.size(); ++i)
                {
                    if (i > 0)
                        note += "+";

                    note += it->recipe->runes[i];
                }
            }

            overlay.Append(note);

            if (it == best)
                overlay.SetColor(OverlayRgb(80, 220, 255));
        }
    }

    if (!settings_.highlightRare)
    {
        markSignature_.clear();
        cachedMarks_.clear();
        return;
    }

    std::string signature;

    for (const ScreenRecipe& entry : found)
    {
        signature += entry.recipe->output;
        signature += ':';
        signature += std::to_string(entry.recipe->count);
        signature += '@';
        signature += std::to_string(frame.rows[entry.rowIndex].textTop / kRowSnap);
        signature += ';';
    }

    if (signature == markSignature_)
    {
        frame.overlay.marks.insert(frame.overlay.marks.end(), cachedMarks_.begin(), cachedMarks_.end());
        return;
    }

    if (!tiles_.Valid())
        tiles_.Analyze(frame.gray, frame.panel, frame.levels);

    if (!tiles_.Valid())
        return;

    std::vector<OverlayMark> marks;
    bool everyRowResolved = true;

    for (const ScreenRecipe& entry : found)
    {
        const std::vector<cv::Rect> tiles =
            tiles_.TilesForRow(frame.gray, frame.rows[entry.rowIndex].textTop, static_cast<int>(entry.recipe->runes.size()));

        if (tiles.empty())
        {
            everyRowResolved = false;
            continue;
        }

        for (size_t i = 0; i < tiles.size(); ++i)
        {
            if (!database_.IsRareRune(entry.recipe->runes[i]))
                continue;

            OverlayMark mark;
            mark.x = frame.region.x + tiles[i].x;
            mark.y = frame.region.y + tiles[i].y;
            mark.width = tiles[i].width;
            mark.height = tiles[i].height;
            mark.color = OverlayRgb(255, 220, 80);

            marks.push_back(mark);
        }
    }

    if (!everyRowResolved)
        tiles_ = RuneTileLocator{};

    markSignature_ = std::move(signature);
    cachedMarks_ = std::move(marks);

    frame.overlay.marks.insert(frame.overlay.marks.end(), cachedMarks_.begin(), cachedMarks_.end());
}

void ExpeditionFeature::DrawTab(UIManager& manager)
{
    if (!database_.Loaded())
    {
        ImGui::TextColored(kYellow, "The combination database is not loaded.");
        ImGui::TextWrapped("Check runehelper.log for the reason; the shipped database is embedded in the binary.");
        return;
    }

    bool changed = false;

    changed |= AtomicCheckbox("Enable Pick Advisor", settings_.enabled);

    ImGui::SameLine();
    ImGui::TextDisabled("%s", DataStatus().c_str());

    if (settings_.enabled)
        changed |= AtomicCheckbox("Show rune names on the overlay", settings_.showRunes);

    if (changed)
        SaveSettings();

    if (!settings_.enabled)
        return;

    ImGui::SeparatorText("ON SCREEN");

    const std::uint64_t version = manager.DebugDataVersion();

    if (!tabBuilt_ || tabVersion_ != version)
    {
        RebuildTabRows(manager.GetDebugData());
        tabVersion_ = version;
        tabBuilt_ = true;
    }

    const std::vector<ExpeditionTabRow>& onScreen = tabRows_;
    const std::vector<std::string>& placed = tabPlaced_;

    if (onScreen.empty())
    {
        ImGui::TextDisabled("No combinations recognised. Point the region at the remnant panel.");
        return;
    }

    if (placed.empty())
    {
        ImGui::TextDisabled("Nothing placed yet");
    }
    else
    {
        std::string placedText;

        for (const auto& rune : placed)
        {
            if (!placedText.empty())
                placedText += " + ";

            placedText += rune;
        }

        ImGui::TextDisabled("Already placed:");
        ImGui::SameLine();
        ImGui::TextColored(kGreen, "%s", placedText.c_str());
    }

    const float available = ImGui::GetContentRegionAvail().y;
    const float minimumHeight = UiScaled(kMinTableHeight);
    const ImVec2 tableSize(0.0f, available > minimumHeight ? available : minimumHeight);

    if (!ImGui::BeginTable(
            "on_screen_table",
            4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
            tableSize
        ))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);

    ImGui::TableSetupColumn("Combo");
    ImGui::TableSetupColumn("Waves", ImGuiTableColumnFlags_WidthFixed, UiScaled(45.0f));
    ImGui::TableSetupColumn("Per wave", ImGuiTableColumnFlags_WidthFixed, UiScaled(70.0f));
    ImGui::TableSetupColumn("Adds");

    ImGui::TableHeadersRow();

    bool first = true;

    for (const auto& entry : onScreen)
    {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);

        if (entry.recipe->count > 1)
            UIDraw::CellText((entry.recipe->output + " x" + std::to_string(entry.recipe->count)).c_str());
        else
            UIDraw::CellText(entry.recipe->output.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%zu", entry.recipe->runes.size());

        ImGui::TableSetColumnIndex(2);

        if (entry.perWave > 0.0)
        {
            if (first)
                ImGui::TextColored(kGreen, "%.2f", entry.perWave);
            else
                ImGui::Text("%.2f", entry.perWave);
        }
        else
        {
            ImGui::TextDisabled("-");
        }

        ImGui::TableSetColumnIndex(3);

        std::string adds;

        for (size_t i = placed.size(); i < entry.recipe->runes.size(); ++i)
        {
            if (!adds.empty())
                adds += ", ";

            adds += entry.recipe->runes[i];
        }

        UIDraw::CellText(adds.c_str());

        first = false;
    }

    ImGui::EndTable();
}

void ExpeditionFeature::DrawMainControls(UIManager& manager)
{
    (void)manager;

    if (!database_.Loaded())
    {
        ImGui::TextDisabled("Expedition: no combination database");
        return;
    }

    if (AtomicCheckbox("Highlight rare runes", settings_.highlightRare))
        SaveSettings();

    if (ImGui::IsItemHovered())
        UiTooltip("Frames the rare rune tiles in the remnant panel. Works without the Pick Advisor.");
}
