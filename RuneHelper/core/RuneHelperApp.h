#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

#include "core/ConfigManager.h"
#include "core/UpdateChecker.h"

#include "ocr/OCR.h"

#include "price/PriceCache.h"

#include "ui/Overlay.h"
#include "ui/UIManager.h"

#include "core/DebugData.h"

class RuneHelperApp
{
public:
    int Run();

private:
    bool Init();
    void Shutdown();

    void MainLoop();

    void InitOcr();
    void OcrWorkerLoop();

    void HandleUIActions();
    void UpdateOverlay();
    void UpdateRegionPreview();

private:
    ConfigManager configManager_;
    AppConfig* config_ = nullptr;

    UIManager ui_;
    OverlayWindow overlay_;
    UpdateChecker updateChecker_;
    PriceCache priceCache_;
    OCR ocr_;

    cv::Rect region_;

    std::mutex overlayMutex_;
    std::vector<OverlayText> sharedTexts_;

    std::atomic<bool> running_ = true;

    std::atomic<bool> ocrReady_ = false;
    std::atomic<bool> ocrFailed_ = false;
    std::atomic<bool> ocrInitializing_ = true;

    std::atomic<bool> singleSnapshotRequested_ = false;
    std::chrono::steady_clock::time_point singleSnapshotUntil_;

    std::atomic<bool> overlayDirty_ = false;

    std::jthread initThread_;
    std::jthread ocrThread_;

    std::mutex debugMutex_;
    DebugData debugData_;
};