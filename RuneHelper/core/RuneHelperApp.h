#pragma once

#include "core/ConfigManager.h"
#include "core/OcrService.h"
#include "core/UpdateChecker.h"
#include "core/Feature.h"
#include "price/PriceService.h"

#include "ui/Overlay.h"
#include "ui/UIManager.h"

class RuneHelperApp
{
public:
    int Run();

private:
    bool Init();
    void Shutdown();

    void MainLoop();

    void HandleUIActions();
    void UpdateOverlay();
    void UpdateRegionPreview(const AppConfig& config);

private:
    ConfigManager configManager_;

    UIManager ui_;
    OverlayWindow overlay_;
    UpdateChecker updateChecker_;
    PriceService prices_;
    FeatureRegistry features_;
    OcrService ocrService_;
};
