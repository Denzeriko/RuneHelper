#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/ConfigManager.h"
#include "core/DebugData.h"
#include "core/ScreenCaptureService.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OcrFrameDiffer.h"
#include "ocr/OCR.h"
#include "ocr/RunePatternMatcher.h"
#include "price/PriceCache.h"
#include "ui/OverlayState.h"

struct OcrServiceStatus
{
    bool initializing = false;
    bool ready = false;
    bool failed = false;
};

struct PriceServiceStatus
{
    bool downloading = false;
    size_t priceCount = 0;
};

class OcrService
{
public:
    OcrService() = default;
    ~OcrService();

    OcrService(const OcrService&) = delete;
    OcrService& operator=(const OcrService&) = delete;

    void Start(ConfigManager& configManager);
    void Stop();

    void RequestSingleSnapshot();
    void RequestRuneCalibration();
    void ForceRefreshPrices();

    OcrServiceStatus GetStatus() const;
    PriceServiceStatus GetPriceStatus() const;
    RunePatternCalibrationStatus GetRuneCalibrationStatus() const;
    DebugData GetDebugData();

    bool ConsumeOverlayTexts(std::vector<OverlayText>& texts);

private:
    void InitOcr();
    void WorkerLoop();

    void ResetState(bool initializing);
    void ClearRuntimeBuffers();
    void ClearOverlayTexts();
    void SaveRuneCalibrationScale(double scale);
    void SetOverlayTexts(std::vector<OverlayText> texts);
    void PublishOverlayTexts(std::vector<OverlayText> texts);

private:
    mutable std::mutex lifecycleMutex_;
    ConfigManager* configManager_ = nullptr;

    PriceCache priceCache_;
    OCR ocr_;
    ScreenCaptureService screenCapture_;
    OcrFrameDiffer frameDiffer_;

    std::atomic<bool> running_ = false;

    std::atomic<bool> ocrReady_ = false;
    std::atomic<bool> ocrFailed_ = false;
    std::atomic<bool> ocrInitializing_ = true;

    std::atomic<bool> singleSnapshotRequested_ = false;
    std::chrono::steady_clock::time_point singleSnapshotUntil_;

    std::atomic<bool> overlayDirty_ = false;
    std::mutex overlayMutex_;
    std::vector<OverlayText> sharedTexts_;
    int emptyOverlayFrames_ = 0;

    std::mutex debugMutex_;
    DebugData debugData_;

    std::mutex cachedNamesMutex_;
    std::shared_ptr<const std::vector<CachedItemName>> cachedItemNames_;

    std::jthread initThread_;
    std::jthread workerThread_;
};
