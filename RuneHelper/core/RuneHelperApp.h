#pragma once

#include "core/ConfigManager.h"
#include "core/Feature.h"
#include "core/OcrService.h"
#include "core/UpdateChecker.h"
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

    void PublishStatus();
    void HandleRequests(const UIRequests& requests);
    void SelectRegion();
    void UpdateOverlay();
    void UpdateRegionPreview(const AppConfig& config);

    ConfigManager configManager_;
    UpdateChecker updateChecker_;
    PriceService prices_;
    FeatureRegistry features_;

    UIManager ui_{ configManager_, updateChecker_, features_ };
    OverlayWindow overlay_;
    OcrService ocrService_{ configManager_, features_, prices_ };
};
