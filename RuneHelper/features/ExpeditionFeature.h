#pragma once

#include <atomic>
#include <cstddef>
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

struct ExpeditionScreenRow
{
    const Recipe* recipe = nullptr;
    double perWave = 0.0;
};

struct ScreenRecipe
{
    const Recipe* recipe = nullptr;
    std::size_t rowIndex = 0;
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

    void DrawSettings(UIManager& manager) override;
    void DrawDebug(UIManager& manager) override;

private:
    std::string DataStatus() const;
    void StoreSettings();

    void HighlightRareRunes(FrameContext& frame, const std::vector<ScreenRecipe>& found);
    void ForgetMarks();

    void DrawAdvisorSettings();
    void RefreshScreenRows(const UIManager& manager);
    void RebuildScreenRows(const DebugData& debug);
    void DrawPlacedRunes() const;
    void DrawScreenRecipeTable() const;

    ConfigManager* configManager_ = nullptr;
    ExpeditionSettings settings_;

    RecipeDatabase database_;
    RecipeUpdater updater_;
    RuneTileLocator tiles_;
    std::atomic<bool> regionDirty_{ false };
    std::string markSignature_;
    std::vector<OverlayMark> cachedMarks_;

    bool screenBuilt_ = false;
    std::uint64_t screenVersion_ = 0;
    std::vector<ExpeditionScreenRow> screenRows_;
    std::vector<std::string> screenPlaced_;
};
