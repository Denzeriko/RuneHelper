#include "features/ExpeditionFeature.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/ConfigManager.h"
#include "core/Logger.h"
#include "ocr/LootParser.h"
#include "price/PriceService.h"
#include "ui/UIManager.h"

namespace
{
constexpr ImVec4 kGreen{ 0.5f, 1.0f, 0.5f, 1.0f };
constexpr ImVec4 kYellow{ 1.0f, 0.8f, 0.2f, 1.0f };

constexpr double kPerWaveTolerance = 0.01;

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

    settings_.enabled = settings.value("enabled", settings_.enabled);
    settings_.showRunes = settings.value("showRunes", settings_.showRunes);
    settings_.highlightRare = settings.value("highlightRare", settings_.highlightRare);

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
    tiles_ = RuneTileLocator{};
}

void ExpeditionFeature::SaveSettings()
{
    if (!configManager_)
        return;

    nlohmann::json settings;
    settings["enabled"] = settings_.enabled;
    settings["showRunes"] = settings_.showRunes;
    settings["highlightRare"] = settings_.highlightRare;

    configManager_->SetFeatureSettings(Name(), std::move(settings));

    if (!configManager_->Save())
        LOG_ERROR("Expedition feature failed to save settings");
}

void ExpeditionFeature::OnFrame(FrameContext& frame)
{
    if (!database_.Loaded())
        return;

    if (!settings_.enabled && !settings_.highlightRare)
        return;

    std::vector<ScreenRecipe> found;

    for (size_t i = 0; i < frame.rows.size(); ++i)
    {
        const FrameRow& row = frame.rows[i];

        ResolvedPrice resolved;

        if (frame.prices)
            resolved = frame.prices->Resolve(row.name, row.quantity);

        const Recipe* recipe = database_.FindRecipe(resolved.name, row.quantity);

        if (!recipe && resolved.name != row.name)
            recipe = database_.FindRecipe(row.name, row.quantity);

        if (!recipe || recipe->runes.empty())
            continue;

        ScreenRecipe entry;
        entry.recipe = recipe;
        entry.rowIndex = i;

        if (resolved.value > 0.0)
            entry.perWave = resolved.value / static_cast<double>(recipe->runes.size());

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
        return;

    if (!tiles_.Valid())
        tiles_.Analyze(frame.gray);

    if (!tiles_.Valid())
        return;

    for (const ScreenRecipe& entry : found)
    {
        const RuneTileBand* band = tiles_.BandAbove(frame.rows[entry.rowIndex].textTop);

        if (!band)
            continue;

        for (size_t i = 0; i < entry.recipe->runes.size(); ++i)
        {
            if (!database_.IsRareRune(entry.recipe->runes[i]))
                continue;

            const cv::Rect tile = tiles_.TileRect(*band, static_cast<int>(i));

            if (tile.x + tile.width > frame.gray.cols)
                break;

            OverlayMark mark;
            mark.x = frame.region.x + tile.x;
            mark.y = frame.region.y + tile.y;
            mark.width = tile.width;
            mark.height = tile.height;
            mark.color = OverlayRgb(255, 220, 80);

            frame.overlay.marks.push_back(mark);
        }
    }
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

    changed |= ImGui::Checkbox("Enable Pick Advisor", &settings_.enabled);

    ImGui::SameLine();
    ImGui::TextDisabled("%s", DataStatus().c_str());

    if (settings_.enabled)
        changed |= ImGui::Checkbox("Show rune names on the overlay", &settings_.showRunes);

    if (changed)
        SaveSettings();

    if (!settings_.enabled)
        return;

    ImGui::SeparatorText("ON SCREEN");

    struct TabRecipe
    {
        const Recipe* recipe;
        double perWave;
    };

    std::vector<TabRecipe> onScreen;

    for (const auto& line : manager.GetDebugData().lines)
    {
        const auto parsed = LootParser::ParseLootLine(line.ocrText);
        const std::string& name = line.matchedText != "-" ? line.matchedText : parsed.itemName;

        const Recipe* recipe = database_.FindRecipe(name, parsed.quantity);

        if (!recipe && name != parsed.itemName)
            recipe = database_.FindRecipe(parsed.itemName, parsed.quantity);

        if (!recipe || recipe->runes.empty())
            continue;

        double perWave = 0.0;

        if (line.price != "-")
        {
            if (const auto value = LootParser::ParsePriceValue(line.price))
                perWave = *value * parsed.quantity / static_cast<double>(recipe->runes.size());
        }

        onScreen.push_back({ recipe, perWave });
    }

    if (onScreen.empty())
    {
        ImGui::TextDisabled("No combinations recognised. Point the region at the remnant panel.");
        return;
    }

    std::vector<std::string> placed = onScreen.front().recipe->runes;

    for (const auto& entry : onScreen)
    {
        size_t common = 0;

        while (common < placed.size() &&
               common < entry.recipe->runes.size() &&
               placed[common] == entry.recipe->runes[common])
        {
            ++common;
        }

        placed.resize(common);
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

    std::sort(
        onScreen.begin(),
        onScreen.end(),
        [](const TabRecipe& a, const TabRecipe& b)
        {
            if (a.perWave != b.perWave)
                return a.perWave > b.perWave;

            return a.recipe->runes.size() < b.recipe->runes.size();
        });

    if (!ImGui::BeginTable("on_screen_table", 4,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp))
    {
        return;
    }

    ImGui::TableSetupColumn("Combo");
    ImGui::TableSetupColumn("Waves", ImGuiTableColumnFlags_WidthFixed, 45.0f);
    ImGui::TableSetupColumn("Per wave", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Adds");

    ImGui::TableHeadersRow();

    bool first = true;

    for (const auto& entry : onScreen)
    {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);

        if (entry.recipe->count > 1)
            ImGui::TextWrapped("%s x%d", entry.recipe->output.c_str(), entry.recipe->count);
        else
            ImGui::TextWrapped("%s", entry.recipe->output.c_str());

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

        ImGui::TextWrapped("%s", adds.c_str());

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

    if (ImGui::Checkbox("Highlight rare runes", &settings_.highlightRare))
        SaveSettings();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Frames the rare rune tiles in the remnant panel. Works without the Pick Advisor.");
}
