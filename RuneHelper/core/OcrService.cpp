#include "core/OcrService.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "core/Logger.h"
#include "core/ThreadGuard.h"
#include "ocr/LootRows.h"
#include "price/PriceService.h"
#include "recipes/RecipeDatabase.h"

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
            if (!RunLoggingExceptions("OcrService init thread", [this] { InitOcr(); }))
            {
                ocrFailed_ = true;
                ocrInitializing_ = false;
            }
        }
    );

    workerThread_ = std::jthread([this] { RunLoggingExceptions("OcrService worker thread", [this] { WorkerLoop(); }); });
}

void OcrService::Stop()
{
    std::lock_guard lifecycleLock(lifecycleMutex_);

    if (!running_.exchange(false) && !initThread_.joinable() && !workerThread_.joinable())
        return;

    screenCapture_.Cancel();

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

void OcrService::RequestDebugDump()
{
    if (!running_.load())
        return;

    debugDumpRequested_ = true;
    forceOcr_ = true;
    singleSnapshotRequested_ = true;
}

OcrServiceStatus OcrService::GetStatus() const
{
    return { ocrInitializing_.load(), ocrReady_.load(), ocrFailed_.load(), captureFailing_.load() };
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

    if (!configManager_ || !LoadLanguage(configManager_->Snapshot().gameLanguage))
    {
        LOG_ERROR("OCR init failed");

        ocrFailed_ = true;
        ocrInitializing_ = false;

        return;
    }

    ocrReady_ = true;
    ocrInitializing_ = false;

    LOG_INFO("OCR ready");
}

bool OcrService::LoadLanguage(const std::string& language)
{
    language_ = language;
    translations_ = CachedItemNames();

    std::string_view model = EmbeddedTextModel(language);

    if (model.empty())
    {
        LOG_ERROR("OCR: this build has no text model for game language '" + language + "', English text is read instead");
        model = EmbeddedTextModel("en");
    }

    if (!ocr_.Init(model))
        return false;

    if (language == "en")
        return true;

    RecipeDatabase recipes;

    if (recipes.Load())
        translations_ = recipes.Translations(language);

    LOG_INFO("OCR: game language '" + language + "', " + std::to_string(translations_.Size()) + " item names to translate");

    return true;
}

void OcrService::ResetFrameState()
{
    frameDiffer_.Reset();
    lastLoot_.clear();
    lastOcrAt_ = {};
    captureFailures_ = 0;
    captureFailing_ = false;
    frameErrorReported_ = false;
    rowCache_.Reset();
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

void OcrService::PublishFrameResult(
    const std::vector<LootLine>& loot,
    const cv::Mat& gray,
    const cv::Rect& region,
    const AppConfig& config
)
{
    std::vector<FrameRow> rows = ParseLootRows(loot, region, config, translations_.Empty() ? nullptr : &translations_);

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
            prices_ ? prices_->DivineRate() : 0.0,
            rowOverlays,
            overlay,
            debug,
            rowCache_.Panel().value_or(cv::Rect(0, 0, gray.cols, gray.rows)),
            rowCache_.Levels(),
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
        text.x = OverlayTextX(region, rowCache_.Panel(), config);
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
        lastLoot_ = ocr_.RecognizeLoot(gray, &rowCache_, debugDumpRequested_.exchange(false));
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

        if (config.gameLanguage != language_)
        {
            LoadLanguage(config.gameLanguage);
            ResetFrameState();
        }

        const cv::Rect region(config.regionX, config.regionY, config.regionW, config.regionH);

        try
        {
            ProcessFrame(region, config);
            frameErrorReported_ = false;
        }
        catch (const std::exception& error)
        {
            if (!frameErrorReported_)
            {
                frameErrorReported_ = true;
                LOG_ERROR(
                    std::string("OcrService: frame dropped after an exception: ") + error.what() +
                    " (further frame errors are not repeated until one succeeds)"
                );
            }
        }
        catch (...)
        {
            if (!frameErrorReported_)
            {
                frameErrorReported_ = true;
                LOG_ERROR("OcrService: frame dropped after an exception of unknown type");
            }
        }

        SleepOcrLoop(running_, singleSnapshotRequested_, kPollIntervalMs);
    }
}

void OcrService::ResetState(bool initializing)
{
    ocrReady_ = false;
    ocrFailed_ = false;
    ocrInitializing_ = initializing;
    singleSnapshotRequested_ = false;
    debugDumpRequested_ = false;
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
