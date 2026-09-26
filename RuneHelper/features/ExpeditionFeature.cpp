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

constexpr OverlayColor kBestRecipeColor = OverlayRgb(80, 220, 255);
constexpr OverlayColor kRareRuneColor = OverlayRgb(255, 220, 80);

struct RuneMarks
{
    std::vector<OverlayMark> marks;
    bool everyRowResolved = true;
};

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

std::string JoinRunes(const std::vector<std::string>& runes, std::size_t first, const char* separator)
{
    std::string joined;

    for (std::size_t i = first; i < runes.size(); ++i)
    {
        if (i > first)
            joined += separator;

        joined += runes[i];
    }

    return joined;
}

const Recipe* FindRecipeFor(const RecipeDatabase& database, const std::string& name, const std::string& readName, int count)
{
    const Recipe* recipe = database.FindRecipe(name, count);

    if (!recipe && name != readName)
        recipe = database.FindRecipe(readName, count);

    return recipe && !recipe->runes.empty() ? recipe : nullptr;
}

std::vector<ScreenRecipe> FindScreenRecipes(const RecipeDatabase& database, const FrameContext& frame)
{
    std::vector<ScreenRecipe> found;

    for (std::size_t i = 0; i < frame.rows.size(); ++i)
    {
        const FrameRow& row = frame.rows[i];
        const ResolvedPrice& resolved = row.price;
        const std::string& matchedName = resolved.name.empty() ? row.name : resolved.name;
        const Recipe* recipe = FindRecipeFor(database, matchedName, row.name, row.quantity);

        if (!recipe)
            continue;

        ScreenRecipe entry;
        entry.recipe = recipe;
        entry.rowIndex = i;

        if (resolved.totalEx > 0.0)
            entry.perWave = resolved.totalEx / static_cast<double>(recipe->runes.size());

        found.push_back(entry);
    }

    return found;
}

const ScreenRecipe* BestPerWave(const std::vector<ScreenRecipe>& found)
{
    const ScreenRecipe* best = nullptr;

    for (const ScreenRecipe& entry : found)
    {
        if (entry.perWave <= 0.0)
            continue;

        if (!best)
        {
            best = &entry;
            continue;
        }

        const double relative = std::abs(entry.perWave - best->perWave) / best->perWave;

        if (relative <= kPerWaveTolerance)
        {
            if (entry.recipe->runes.size() < best->recipe->runes.size())
                best = &entry;
        }
        else if (entry.perWave > best->perWave)
        {
            best = &entry;
        }
    }

    return best;
}

std::string RecipeNote(const ScreenRecipe& entry, bool showRunes)
{
    std::string note = std::to_string(entry.recipe->runes.size()) + "w";

    if (entry.perWave > 0.0)
    {
        note += " ";
        note += FormatPerWave(entry.perWave);
    }

    if (showRunes)
    {
        note += "  ";
        note += JoinRunes(entry.recipe->runes, 0, "+");
    }

    return note;
}

void AnnotateRecipes(FrameContext& frame, const std::vector<ScreenRecipe>& found, bool showRunes)
{
    const ScreenRecipe* best = BestPerWave(found);

    for (const ScreenRecipe& entry : found)
    {
        RowOverlay& overlay = frame.rowOverlays[entry.rowIndex];

        overlay.Append(RecipeNote(entry, showRunes));

        if (&entry == best)
            overlay.SetColor(kBestRecipeColor);
    }
}

std::string MarkSignature(const FrameContext& frame, const std::vector<ScreenRecipe>& found)
{
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

    return signature;
}

RuneMarks RareRuneMarks(
    const RuneTileLocator& tiles,
    const RecipeDatabase& database,
    const FrameContext& frame,
    const std::vector<ScreenRecipe>& found
)
{
    RuneMarks result;

    for (const ScreenRecipe& entry : found)
    {
        const std::vector<cv::Rect> rowTiles =
            tiles.TilesForRow(frame.gray, frame.rows[entry.rowIndex].textTop, static_cast<int>(entry.recipe->runes.size()));

        if (rowTiles.empty())
        {
            result.everyRowResolved = false;
            continue;
        }

        for (std::size_t i = 0; i < rowTiles.size(); ++i)
        {
            if (!database.IsRareRune(entry.recipe->runes[i]))
                continue;

            OverlayMark mark;
            mark.x = frame.region.x + rowTiles[i].x;
            mark.y = frame.region.y + rowTiles[i].y;
            mark.width = rowTiles[i].width;
            mark.height = rowTiles[i].height;
            mark.color = kRareRuneColor;

            result.marks.push_back(mark);
        }
    }

    return result;
}

void DrawPerWave(double perWave, bool best)
{
    if (perWave <= 0.0)
    {
        ImGui::TextDisabled("-");
        return;
    }

    if (best)
        ImGui::TextColored(kGreen, "%.2f", perWave);
    else
        ImGui::Text("%.2f", perWave);
}
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

void ExpeditionFeature::StoreSettings()
{
    nlohmann::json settings;
    settings["enabled"] = settings_.enabled.load();
    settings["showRunes"] = settings_.showRunes.load();
    settings["highlightRare"] = settings_.highlightRare.load();

    configManager_->SetFeatureSettings(Name(), std::move(settings));
}

void ExpeditionFeature::OnFrame(FrameContext& frame)
{
    if (regionDirty_.exchange(false))
    {
        tiles_ = RuneTileLocator{};
        ForgetMarks();
    }

    if (!database_.Loaded() || (!settings_.enabled && !settings_.highlightRare))
        return;

    const std::vector<ScreenRecipe> found = FindScreenRecipes(database_, frame);

    if (found.empty())
        return;

    if (settings_.enabled)
        AnnotateRecipes(frame, found, settings_.showRunes);

    if (settings_.highlightRare)
        HighlightRareRunes(frame, found);
    else
        ForgetMarks();
}

void ExpeditionFeature::HighlightRareRunes(FrameContext& frame, const std::vector<ScreenRecipe>& found)
{
    std::string signature = MarkSignature(frame, found);

    if (signature != markSignature_)
    {
        if (!tiles_.Valid())
            tiles_.Analyze(frame.gray, frame.panel, frame.levels);

        if (!tiles_.Valid())
            return;

        RuneMarks rareRunes = RareRuneMarks(tiles_, database_, frame, found);

        if (!rareRunes.everyRowResolved)
            tiles_ = RuneTileLocator{};

        markSignature_ = std::move(signature);
        cachedMarks_ = std::move(rareRunes.marks);
    }

    frame.overlay.marks.insert(frame.overlay.marks.end(), cachedMarks_.begin(), cachedMarks_.end());
}

void ExpeditionFeature::ForgetMarks()
{
    markSignature_.clear();
    cachedMarks_.clear();
}

void ExpeditionFeature::DrawTab(UIManager& manager)
{
    if (!database_.Loaded())
    {
        ImGui::TextColored(kYellow, "The combination database is not loaded.");
        ImGui::TextWrapped("Check runehelper.log for the reason; the shipped database is embedded in the binary.");
        return;
    }

    DrawAdvisorSettings();

    if (!settings_.enabled)
        return;

    ImGui::SeparatorText("ON SCREEN");

    RefreshTabRows(manager);

    if (tabRows_.empty())
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("No combinations recognised. Point the region at the remnant panel.");
        ImGui::PopTextWrapPos();
        return;
    }

    DrawPlacedRunes();
    DrawRecipeTable();
}

void ExpeditionFeature::DrawAdvisorSettings()
{
    bool changed = AtomicCheckbox("Enable Pick Advisor", settings_.enabled);

    ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", DataStatus().c_str());
    ImGui::PopTextWrapPos();

    if (settings_.enabled)
        changed |= AtomicCheckbox("Show rune names on the overlay", settings_.showRunes);

    if (changed)
        StoreSettings();
}

void ExpeditionFeature::RefreshTabRows(const UIManager& manager)
{
    const std::uint64_t version = manager.DebugDataVersion();

    if (tabBuilt_ && tabVersion_ == version)
        return;

    RebuildTabRows(manager.GetDebugData());
    tabVersion_ = version;
    tabBuilt_ = true;
}

void ExpeditionFeature::RebuildTabRows(const DebugData& debug)
{
    tabRows_.clear();
    tabPlaced_.clear();

    for (const auto& line : debug.lines)
    {
        const auto parsed = LootParser::ParseLootLine(line.ocrText);
        const std::string& name = line.Matched() ? line.matchedText : parsed.itemName;
        const Recipe* recipe = FindRecipeFor(database_, name, parsed.itemName, parsed.quantity);

        if (!recipe)
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

void ExpeditionFeature::DrawPlacedRunes() const
{
    if (tabPlaced_.empty())
    {
        ImGui::TextDisabled("Nothing placed yet");
        return;
    }

    ImGui::TextDisabled("Already placed:");
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kGreen, "%s", JoinRunes(tabPlaced_, 0, " + ").c_str());
    ImGui::PopTextWrapPos();
}

void ExpeditionFeature::DrawRecipeTable() const
{
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

    for (const auto& entry : tabRows_)
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
        DrawPerWave(entry.perWave, first);

        ImGui::TableSetColumnIndex(3);
        UIDraw::CellText(JoinRunes(entry.recipe->runes, tabPlaced_.size(), ", ").c_str());

        first = false;
    }

    ImGui::EndTable();
}

void ExpeditionFeature::DrawMainControls(UIManager&)
{
    if (!database_.Loaded())
    {
        ImGui::TextDisabled("Expedition: no combination database");
        return;
    }

    if (AtomicCheckbox("Highlight rare runes", settings_.highlightRare))
        StoreSettings();

    if (ImGui::IsItemHovered())
        UiTooltip("Frames the rare rune tiles in the remnant panel. Works without the Pick Advisor.");
}
