#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/ConfigManager.h"
#include "core/DebugData.h"
#include "core/Feature.h"
#include "core/OcrState.h"
#include "core/ScreenCaptureService.h"
#include "ocr/NameMatcher.h"
#include "ocr/OcrFrameDiffer.h"
#include "ocr/OcrRowCache.h"
#include "ocr/OCR.h"
#include "recipes/RecipeDatabase.h"
#include "ui/OverlayState.h"

class PriceService;

class OcrService
{
public:
    OcrService(ConfigManager& configManager, FeatureRegistry& features, PriceService& prices);
    ~OcrService();

    OcrService(const OcrService&) = delete;
    OcrService& operator=(const OcrService&) = delete;

    void Start();
    void Stop();

    void RequestSingleSnapshot();
    void RequestDebugDump();

    OcrStatus Status() const;
    unsigned DebugDumpsWritten() const;
    bool ConsumeDebugData(DebugData& data);

    bool ConsumeOverlayFrame(OverlayFrame& frame);

private:
    bool InitOcr();
    bool LoadLanguage(const std::string& language);
    void WorkerLoop();

    void ResetFrameState();
    bool PauseForGame(const AppConfig& config, bool snapshot);
    void ProcessFrame(const cv::Rect& region, const AppConfig& config);
    bool NeedsOcr(const cv::Mat& gray);
    void PublishFrameResult(const std::vector<LootLine>& loot, const cv::Mat& gray, const cv::Rect& region, const AppConfig& config);

    void ResetState(OcrState state);
    void ClearRuntimeBuffers();
    void ClearOverlayTexts();
    void SetOverlayFrame(OverlayFrame frame);
    void PublishOverlayFrame(OverlayFrame frame);

    ConfigManager& configManager_;
    FeatureRegistry& features_;
    PriceService& prices_;

    OCR ocr_;
    std::string language_;
    NameMatcher translations_;
    RecipeDatabase recipes_;
    ScreenCaptureService screenCapture_;
    OcrFrameDiffer frameDiffer_;
    OcrRowCache rowCache_;

    std::mutex overlayMutex_;
    OverlayFrame sharedFrame_;
    std::mutex debugMutex_;
    DebugData debugData_;

    std::vector<LootLine> lastLoot_;
    std::chrono::steady_clock::time_point lastOcrAt_{};
    std::chrono::steady_clock::time_point singleSnapshotUntil_;
    int emptyOverlayFrames_ = 0;
    int captureFailures_ = 0;
    std::atomic<unsigned> debugDumpsWritten_ = 0;

    std::atomic<OcrState> state_ = OcrState::Initializing;
    std::atomic<bool> running_ = false;
    std::atomic<bool> captureFailing_ = false;
    std::atomic<bool> waitingForGame_ = false;
    std::atomic<bool> singleSnapshotRequested_ = false;
    std::atomic<bool> debugDumpRequested_ = false;
    std::atomic<bool> forceOcr_ = false;
    std::atomic<bool> overlayDirty_ = false;
    std::atomic<bool> debugDirty_ = false;
    bool frameErrorReported_ = false;

    std::jthread workerThread_;
};
