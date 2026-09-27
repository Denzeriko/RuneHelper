#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "core/ConfigManager.h"
#include "core/DebugData.h"
#include "core/Feature.h"
#include "core/OcrPipeline.h"
#include "core/OcrState.h"
#include "core/ScreenCaptureService.h"
#include "ocr/OcrFrameDiffer.h"
#include "ocr/OcrRowCache.h"
#include "ui/OverlayState.h"

class PriceService;

class OcrService
{
public:
    OcrService(
        ConfigManager& configManager,
        FeatureRegistry& features,
        PriceService& prices,
        std::unique_ptr<IScreenCapture> screenCapture = {}
    );
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
    enum class Command
    {
        SingleSnapshot,
        DebugDump
    };

    bool InitOcr();
    void WorkerLoop(const std::stop_token& stop);
    void Enqueue(Command command);
    bool DrainCommands();
    void WaitForWork(int milliseconds);

    void ResetFrameState();
    bool PauseForGame(const AppConfig& config, bool snapshot);
    void ProcessFrame(const cv::Rect& region, const AppConfig& config, const std::stop_token& stop);
    bool NeedsOcr(const cv::Mat& gray);

    void ResetState(OcrState state);
    void ClearRuntimeBuffers();
    void ClearOverlayTexts();
    void SetOverlayFrame(OverlayFrame frame);
    void PublishOverlayFrame(OverlayFrame frame);

    ConfigManager& configManager_;
    OcrPipeline pipeline_;
    std::string language_;
    std::unique_ptr<IScreenCapture> screenCapture_;
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
    std::mutex commandMutex_;
    std::condition_variable commandCondition_;
    std::deque<Command> commands_;
    bool debugDumpRequested_ = false;
    bool forceOcr_ = false;
    std::atomic<bool> overlayDirty_ = false;
    std::atomic<bool> debugDirty_ = false;
    bool frameErrorReported_ = false;

    std::jthread workerThread_;
};
