#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "core/Config.h"
#include "core/DebugData.h"
#include "ocr/LootRows.h"
#include "ui/OverlayState.h"

class ConfigManager;
class UIManager;

struct RowOverlay
{
    std::string note;
    bool hasColor = false;
    OverlayColor color = OverlayRgb(160, 160, 160);

    void Append(const std::string& part)
    {
        if (!note.empty())
            note += "  ";

        note += part;
    }

    void SetColor(OverlayColor value)
    {
        color = value;
        hasColor = true;
    }
};

struct FrameContext
{
    const cv::Mat& gray;
    const cv::Rect& region;
    const std::vector<FrameRow>& rows;
    const AppConfig& config;
    double divineToEx = 0.0;

    std::vector<RowOverlay>& rowOverlays;
    OverlayFrame& overlay;
    DebugData& debug;
};

class Feature
{
public:
    virtual ~Feature() = default;

    virtual std::string Name() const = 0;

    virtual bool Init(ConfigManager& configManager) { (void)configManager; return true; }
    virtual void Shutdown() {}

    virtual void OnRegionChanged() {}
    virtual void OnFrame(FrameContext& frame) { (void)frame; }

    virtual void DrawMainControls(UIManager& manager) { (void)manager; }

    virtual const char* TabTitle() const { return nullptr; }
    virtual void DrawTab(UIManager& manager) { (void)manager; }
};

class FeatureRegistry
{
public:
    void Add(std::unique_ptr<Feature> feature);

    bool InitAll(ConfigManager& configManager);
    void ShutdownAll();

    void NotifyRegionChanged();
    void RunFrame(FrameContext& frame);

    const std::vector<std::unique_ptr<Feature>>& All() const { return features_; }

private:
    std::vector<std::unique_ptr<Feature>> features_;
};
