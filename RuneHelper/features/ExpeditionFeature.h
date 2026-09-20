#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "core/Feature.h"
#include "ocr/RuneTileLocator.h"
#include "recipes/RecipeDatabase.h"
#include "recipes/RecipeUpdater.h"

struct ExpeditionSettings
{
    std::atomic<bool> enabled = true;
    std::atomic<bool> showRunes = true;
    std::atomic<bool> highlightRare = true;
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

    ConfigManager* configManager_ = nullptr;
    ExpeditionSettings settings_;

    RecipeDatabase database_;
    RecipeUpdater updater_;
    RuneTileLocator tiles_;
    std::atomic<bool> regionDirty_{ false };
    std::string markSignature_;
    std::vector<OverlayMark> cachedMarks_;
};
