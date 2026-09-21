#include "core/OcrService.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <thread>
#include <utility>

#include "core/Logger.h"
#include "ocr/LootRows.h"
#include "price/PriceService.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

namespace
{
constexpr int kPollIntervalMs = 100;
constexpr int kMinOcrGapMs = 600;
constexpr int kMaxOcrDelayMs = 1500;
constexpr int kOcrSleepChunkMs = 50;
constexpr int kEmptyOverlayFramesBeforeClear = 3;
constexpr int kCaptureFailuresBeforeWarning = 3;

void SleepOcrLoop(std::atomic<bool>& running, const std::atomic<bool>& singleSnapshotRequested, int sleepMs)
{
    int remainingMs = sleepMs;
    while (running && remainingMs > 0 && !singleSnapshotRequested.load())
    {
        const int chunkMs = std::min(remainingMs, kOcrSleepChunkMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(chunkMs));
        remainingMs -= chunkMs;
    }
}

constexpr int kOverlayYJitter = 3;
}

OcrService::~OcrService()
{
    Stop();
}

void OcrService::Start(ConfigManager& configManager, FeatureRegistry& features, PriceService& prices)
{
    std::lock_guard lifecycleLock(lifecycleMutex_);

    if (running_.load())
    {
        LOG_INFO("OcrService::Start() ignored because service is already running");
        return;
    }

    configManager_ = &configManager;
    features_ = &features;
    prices_ = &prices;
    running_ = true;
    ResetState(true);

    initThread_ = std::jthread(
        [this]
        {
            InitOcr();
        });

    workerThread_ = std::jthread(
        [this]
        {
            WorkerLoop();
        });
}

void OcrService::Stop()
{
    std::lock_guard lifecycleLock(lifecycleMutex_);

    if (!running_.exchange(false) && !initThread_.joinable() && !workerThread_.joinable())
        return;

    if (initThread_.joinable())
        initThread_.join();

    if (workerThread_.joinable())
        workerThread_.join();

    screenCapture_.Shutdown();
    configManager_ = nullptr;
    features_ = nullptr;
    prices_ = nullptr;
    ResetState(false);
}

void OcrService::RequestSingleSnapshot()
{
    if (!running_.load())
        return;

    forceOcr_ = true;
    singleSnapshotRequested_ = true;
}

OcrServiceStatus OcrService::GetStatus() const
{
    return {
        ocrInitializing_.load(),
        ocrReady_.load(),
        ocrFailed_.load(),
        captureFailing_.load()
    };
}

bool OcrService::ConsumeDebugData(DebugData& data)
{
    if (!debugDirty_.exchange(false))
        return false;

    std::lock_guard lock(debugMutex_);
    data = debugData_;

    return true;
}

bool OcrService::ConsumeOverlayFrame(OverlayFrame& frame)
{
    if (!overlayDirty_.exchange(false))
        return false;

    std::lock_guard lock(overlayMutex_);
    frame = sharedFrame_;
    return true;
}

void OcrService::InitOcr()
{
    ocrInitializing_ = true;

    LOG_INFO("Initializing OCR");

    std::string tessdata = PrepareTessdata();

    if (!ocr_.Init(tessdata))
    {
        LOG_ERROR("Tesseract init failed");

        ocrFailed_ = true;
        ocrInitializing_ = false;

        return;
    }

    ocrReady_ = true;
    ocrInitializing_ = false;

    LOG_INFO("OCR ready");
}

void OcrService::ResetFrameState()
{
    frameDiffer_.Reset();
    lastLoot_.clear();
    lastOcrAt_ = {};
    captureFailures_ = 0;
    captureFailing_ = false;
}

namespace
{
constexpr int kOverlayRowSpacing = 25;

bool HasCloseOverlayText(const std::vector<OverlayText>& texts, int y, int minDistance)
{
    for (const auto& text : texts)
    {
        if (std::abs(text.y - y) < minDistance)
            return true;
    }

    return false;
}
}

void OcrService::PublishFrameResult(const std::vector<LootLine>& loot, const cv::Mat& gray, const cv::Rect& region, const AppConfig& config)
{
    std::vector<FrameRow> rows = ParseLootRows(loot, region, config);

    if (prices_ && config.priceSearchEnabled)
    {
        for (FrameRow& row : rows)
            row.price = prices_->Resolve(row.name, row.quantity);
    }

    DebugData debug;
    debug.lines.reserve(rows.size());

    for (const auto& item : loot)
    {
        DebugLine line;
        line.ocrText = item.text;
        line.matchedText = "-";
        line.price = "-";
        line.confidence = 0;

        debug.lines.push_back(std::move(line));
    }

    std::vector<RowOverlay> rowOverlays(rows.size());
    OverlayFrame overlay;

    if (features_)
    {
        FrameContext frame{
            gray,
            region,
            rows,
            config,
            rowOverlays,
            overlay,
            debug
        };

        features_->RunFrame(frame);
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rowOverlays[i].note.empty())
            continue;

        const int y = rows[i].overlayY;

        if (HasCloseOverlayText(overlay.texts, y, kOverlayRowSpacing))
            continue;

        OverlayText text;
        text.text = rowOverlays[i].note;
        text.color = rowOverlays[i].color;
        text.x = region.x + region.width + config.overlayOffsetX;
        text.y = y;

        overlay.texts.push_back(std::move(text));
    }

    PublishOverlayFrame(std::move(overlay));

    {
        std::lock_guard lock(debugMutex_);
        debugData_ = std::move(debug);
    }

    debugDirty_ = true;
}

void OcrService::ProcessFrame(const cv::Rect& region, const AppConfig& config)
{
    cv::Mat gray = screenCapture_.CaptureRegion(region);

    if (gray.empty())
    {
        if (captureFailures_ < kCaptureFailuresBeforeWarning)
            ++captureFailures_;

        if (captureFailures_ >= kCaptureFailuresBeforeWarning)
            captureFailing_ = true;

        return;
    }

    captureFailures_ = 0;
    captureFailing_ = false;

    if (NeedsOcr(gray))
    {
        lastLoot_ = ocr_.RecognizeLoot(gray, config);
        frameDiffer_.StoreOcrFrame(gray);
        lastOcrAt_ = std::chrono::steady_clock::now();
    }

    PublishFrameResult(lastLoot_, gray, region, config);

    frameDiffer_.StoreFrame(std::move(gray));
}

bool OcrService::NeedsOcr(const cv::Mat& gray)
{
    if (forceOcr_.exchange(false))
        return true;

    if (!frameDiffer_.ChangedSinceOcr(gray))
        return false;

    const auto sinceOcr = std::chrono::steady_clock::now() - lastOcrAt_;

    if (sinceOcr < std::chrono::milliseconds(kMinOcrGapMs))
        return false;

    return frameDiffer_.IsSettled(gray) || sinceOcr >= std::chrono::milliseconds(kMaxOcrDelayMs);
}

void OcrService::WorkerLoop()
{
    while (running_ && !ocrReady_)
    {
        if (ocrFailed_)
            return;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }


    while (running_)
    {
        if (!configManager_)
            return;

        const AppConfig config = configManager_->Snapshot();

        const bool snapshotRequested = singleSnapshotRequested_.exchange(false);

        if (snapshotRequested)
            singleSnapshotUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        const bool keepSnapshot = std::chrono::steady_clock::now() < singleSnapshotUntil_;
        if (!config.ocrEnabled && !snapshotRequested && !keepSnapshot)
        {
            ResetFrameState();
            ClearOverlayTexts();
            SleepOcrLoop(running_, singleSnapshotRequested_, 100);

            continue;
        }

        if (config.regionW <= 0 || config.regionH <= 0)
        {
            ResetFrameState();
            SleepOcrLoop(running_, singleSnapshotRequested_, 100);

            continue;
        }

        const cv::Rect region(config.regionX, config.regionY, config.regionW, config.regionH);

        ProcessFrame(region, config);

        SleepOcrLoop(running_, singleSnapshotRequested_, kPollIntervalMs);
    }
}

void OcrService::ResetState(bool initializing)
{
    ocrReady_ = false;
    ocrFailed_ = false;
    ocrInitializing_ = initializing;
    singleSnapshotRequested_ = false;
    singleSnapshotUntil_ = {};
    overlayDirty_ = false;
    debugDirty_ = false;
    emptyOverlayFrames_ = 0;
    forceOcr_ = false;
    ResetFrameState();
    ClearRuntimeBuffers();
}

void OcrService::ClearRuntimeBuffers()
{
    {
        std::lock_guard lock(overlayMutex_);
        sharedFrame_ = {};
    }

    {
        std::lock_guard lock(debugMutex_);
        debugData_ = {};
    }
}

void OcrService::ClearOverlayTexts()
{
    emptyOverlayFrames_ = 0;
    SetOverlayFrame({});
}

void OcrService::SetOverlayFrame(OverlayFrame frame)
{
    std::lock_guard lock(overlayMutex_);

    if (sharedFrame_.ApproxEquals(frame, kOverlayYJitter))
        return;

    sharedFrame_ = std::move(frame);
    overlayDirty_ = true;
}

void OcrService::PublishOverlayFrame(OverlayFrame frame)
{
    if (!frame.Empty())
    {
        emptyOverlayFrames_ = 0;
        SetOverlayFrame(std::move(frame));
        return;
    }

    ++emptyOverlayFrames_;

    if (emptyOverlayFrames_ >= kEmptyOverlayFramesBeforeClear)
        SetOverlayFrame({});
}
