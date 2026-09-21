#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Feature.h"
#include "ocr/RuneTileLocator.h"
#include "recipes/RecipeDatabase.h"
#include "recipes/RecipeUpdater.h"

struct ExpeditionSettings
{
    std::atomic<bool> enabled = false;
    std::atomic<bool> showRunes = true;
    std::atomic<bool> highlightRare = false;
};

struct ExpeditionTabRow
{
    const Recipe* recipe = nullptr;
    double perWave = 0.0;
};

class ExpeditionFeature : public Feature
{
public:
    std::string Name() const override { return "expedition"; }

    bool Init(ConfigManager& configManager) override;
    void Shutdown() override;

    void OnRegionChanged() override;
    void OnFrame(FrameContext& frame) override;

    void DrawMainControls(UIManager& manager) override;

    const char* TabTitle() const override { return "Expedition"; }
    void DrawTab(UIManager& manager) override;

private:
    std::string DataStatus() const;
    void SaveSettings();
    void RebuildTabRows(const DebugData& debug);

    ConfigManager* configManager_ = nullptr;
    ExpeditionSettings settings_;

    RecipeDatabase database_;
    RecipeUpdater updater_;
    RuneTileLocator tiles_;
    std::atomic<bool> regionDirty_{ false };
    std::string markSignature_;
    std::vector<OverlayMark> cachedMarks_;

    bool tabBuilt_ = false;
    std::uint64_t tabVersion_ = 0;
    std::vector<ExpeditionTabRow> tabRows_;
    std::vector<std::string> tabPlaced_;
};
