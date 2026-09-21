#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/ConfigManager.h"
#include "core/DebugData.h"
#include "core/ScreenCaptureService.h"
#include "ocr/OcrFrameDiffer.h"
#include "ocr/OcrRowCache.h"
#include "ocr/OCR.h"
#include "core/Feature.h"
#include "ui/OverlayState.h"

class PriceService;

struct OcrServiceStatus
{
    bool initializing = false;
    bool ready = false;
    bool failed = false;
    bool captureFailing = false;
};


class OcrService
{
public:
    OcrService() = default;
    ~OcrService();

    OcrService(const OcrService&) = delete;
    OcrService& operator=(const OcrService&) = delete;

    void Start(ConfigManager& configManager, FeatureRegistry& features, PriceService& prices);
    void Stop();

    void RequestSingleSnapshot();

    OcrServiceStatus GetStatus() const;
    bool ConsumeDebugData(DebugData& data);

    bool ConsumeOverlayFrame(OverlayFrame& frame);

private:
    void InitOcr();
    void WorkerLoop();

    void ResetFrameState();
    void ProcessFrame(const cv::Rect& region, const AppConfig& config);
    bool NeedsOcr(const cv::Mat& gray);
    void PublishFrameResult(const std::vector<LootLine>& loot, const cv::Mat& gray, const cv::Rect& region, const AppConfig& config);

    void ResetState(bool initializing);
    void ClearRuntimeBuffers();
    void ClearOverlayTexts();
    void SetOverlayFrame(OverlayFrame frame);
    void PublishOverlayFrame(OverlayFrame frame);

private:
    mutable std::mutex lifecycleMutex_;
    ConfigManager* configManager_ = nullptr;
    FeatureRegistry* features_ = nullptr;

    PriceService* prices_ = nullptr;
    OCR ocr_;
    ScreenCaptureService screenCapture_;
    OcrFrameDiffer frameDiffer_;
    OcrRowCache rowCache_;

    std::atomic<bool> running_ = false;

    std::atomic<bool> ocrReady_ = false;
    std::atomic<bool> ocrFailed_ = false;
    std::atomic<bool> ocrInitializing_ = true;
    std::atomic<bool> captureFailing_ = false;

    std::atomic<bool> singleSnapshotRequested_ = false;
    std::chrono::steady_clock::time_point singleSnapshotUntil_;

    std::atomic<bool> overlayDirty_ = false;
    std::mutex overlayMutex_;
    OverlayFrame sharedFrame_;
    int emptyOverlayFrames_ = 0;

    std::atomic<bool> debugDirty_ = false;
    std::mutex debugMutex_;
    DebugData debugData_;


    std::vector<LootLine> lastLoot_;
    std::atomic<bool> forceOcr_ = false;
    std::chrono::steady_clock::time_point lastOcrAt_{};
    int captureFailures_ = 0;
    bool frameErrorReported_ = false;

    std::jthread initThread_;
    std::jthread workerThread_;
};
