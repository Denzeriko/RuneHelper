#include "core/OcrService.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <memory>
#include <thread>
#include <utility>

#include "core/Logger.h"
#include "ocr/LootOverlayBuilder.h"
#include "ocr/RunePatternMatcher.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

namespace
{
constexpr int kStableOcrFramesBeforeReuse = 3;
constexpr int kMaxStableOcrIntervalMs = 2000;
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

bool EqualOverlayText(const OverlayText& a, const OverlayText& b)
{
    return a.x == b.x && a.y == b.y && a.color == b.color && a.text == b.text;
}

bool EqualOverlayTexts(const std::vector<OverlayText>& a, const std::vector<OverlayText>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
    {
        if (!EqualOverlayText(a[i], b[i]))
            return false;
    }

    return true;
}
}

OcrService::~OcrService()
{
    Stop();
}

void OcrService::Start(ConfigManager& configManager)
{
    std::lock_guard lifecycleLock(lifecycleMutex_);

    if (running_.load())
    {
        LOG_INFO("OcrService::Start() ignored because service is already running");
        return;
    }

    configManager_ = &configManager;
    running_ = true;
    ResetState(true);

    const AppConfig config = configManager.Snapshot();

    priceCache_.SetRefreshMinutes(config.priceRefreshMinutes);
    priceCache_.SetLeague(config.priceLeague);
    PrepareRuneTemplates();
    runeMatcher_.SetSearchScale(config.runeSearchScale);
    if (config.priceSearchEnabled)
        priceCache_.RefreshIfNeeded();

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
    ResetState(false);
}

void OcrService::RequestSingleSnapshot()
{
    if (!running_.load())
        return;

    singleSnapshotRequested_ = true;
}

void OcrService::RequestRuneCalibration()
{
    if (!running_.load())
        return;

    runeMatcher_.BeginScaleCalibration();
    singleSnapshotRequested_ = true;
}

void OcrService::ForceRefreshPrices()
{
    if (!running_.load())
        return;

    priceCache_.ForceRefreshAsync();
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

PriceServiceStatus OcrService::GetPriceStatus() const
{
    return {
        priceCache_.IsRefreshInProgress(),
        priceCache_.GetPriceCount()
    };
}

RunePatternCalibrationStatus OcrService::GetRuneCalibrationStatus() const
{
    return runeMatcher_.CalibrationStatus();
}

bool OcrService::ConsumeDebugData(DebugData& data)
{
    if (!debugDirty_.exchange(false))
        return false;

    std::lock_guard lock(debugMutex_);
    data = debugData_;

    return true;
}

bool OcrService::ConsumeOverlayTexts(std::vector<OverlayText>& texts)
{
    if (!overlayDirty_.exchange(false))
        return false;

    std::lock_guard lock(overlayMutex_);
    texts = sharedTexts_;
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

void OcrService::RebuildCachedNames()
{
    const std::uint64_t version = priceCache_.Version();

    std::lock_guard lock(cachedNamesMutex_);

    cachedItemNames_ = std::make_shared<const CachedItemNames>(
        CachedItemNames::Build(priceCache_.GetAllItemNames()));

    cachedNamesVersion_ = version;
}

void OcrService::ResetFrameState()
{
    frameDiffer_.Reset();
    lastLoot_.clear();
    lastRunes_.clear();
    lastRunesValid_ = false;
    captureFailures_ = 0;
    captureFailing_ = false;
}

int OcrService::NextSleepMs(const AppConfig& config) const
{
    if (frameDiffer_.StableFrames() >= kStableOcrFramesBeforeReuse)
        return std::min(config.ocrIntervalMs * 2, kMaxStableOcrIntervalMs);

    return config.ocrIntervalMs;
}

void OcrService::UpdateRuneMatches(const cv::Mat& gray, const AppConfig& config, bool stableFrame, bool calibrationRunning)
{
    if (!config.runeSearchEnabled || calibrationRunning)
    {
        lastRunes_.clear();
        lastRunesValid_ = false;

        return;
    }

    if (stableFrame && lastRunesValid_)
        return;

    lastRunes_ = runeMatcher_.Find(gray);
    lastRunesValid_ = true;
}

void OcrService::PublishFrameResult(const std::vector<LootLine>& loot, const cv::Rect& region, const AppConfig& config)
{
    std::shared_ptr<const CachedItemNames> cachedNames;
    {
        std::lock_guard lock(cachedNamesMutex_);
        cachedNames = cachedItemNames_;
    }

    static const CachedItemNames kNoNames;

    LootOverlayBuildResult buildResult = LootOverlayBuilder::Build(
        loot,
        region,
        config,
        priceCache_,
        cachedNames ? *cachedNames : kNoNames
    );

    for (const auto& runeMatch : lastRunes_)
    {
        OverlayText text;
        text.text = std::wstring(runeMatch.label.begin(), runeMatch.label.end());
        text.color = OverlayRgb(60, 255, 60);
        text.x = region.x + runeMatch.rect.x + runeMatch.rect.width / 2 - config.overlayFontSize / 4;
        text.y = region.y + runeMatch.rect.y + runeMatch.rect.height / 2;
        buildResult.texts.push_back(std::move(text));
    }

    PublishOverlayTexts(std::move(buildResult.texts));

    {
        std::lock_guard lock(debugMutex_);
        debugData_ = std::move(buildResult.debug);
    }

    debugDirty_ = true;
}

bool OcrService::ProcessFrame(const cv::Rect& region, const AppConfig& config, bool calibrationRunning)
{
    const cv::Mat img = screenCapture_.CaptureRegion(region);

    if (img.empty())
    {
        if (captureFailures_ < kCaptureFailuresBeforeWarning)
            ++captureFailures_;

        if (captureFailures_ >= kCaptureFailuresBeforeWarning)
            captureFailing_ = true;

        return calibrationRunning;
    }

    captureFailures_ = 0;
    captureFailing_ = false;

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    runeMatcher_.StepScaleCalibration(gray);

    const RunePatternCalibrationStatus status = runeMatcher_.CalibrationStatus();

    if (runeCalibrationWasRunning_ && !status.running && status.bestScale > 0.0)
        SaveRuneCalibrationScale(status.bestScale);

    const bool similarFrame = frameDiffer_.IsSimilarFrame(gray, forceOcrFrame_);
    const bool stableFrame = similarFrame && frameDiffer_.StableFrames() >= kStableOcrFramesBeforeReuse;

    UpdateRuneMatches(gray, config, stableFrame, status.running);

    std::vector<LootLine> loot;

    if (stableFrame && !lastLoot_.empty())
    {
        loot = lastLoot_;
    }
    else
    {
        loot = ocr_.RecognizeLoot(img, gray, config, lastRunes_);
        lastLoot_ = loot;
        forceOcrFrame_ = false;
    }

    frameDiffer_.StoreFrame(std::move(gray));

    PublishFrameResult(loot, region, config);

    return status.running;
}

void OcrService::WorkerLoop()
{
    while (running_ && !ocrReady_)
    {
        if (ocrFailed_)
            return;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    RebuildCachedNames();

    auto lastRefreshCheck = std::chrono::steady_clock::now();

    while (running_)
    {
        if (!configManager_)
            return;

        const AppConfig config = configManager_->Snapshot();

        priceCache_.SetRefreshMinutes(config.priceRefreshMinutes);
        priceCache_.SetLeague(config.priceLeague);

        if (config.priceSearchEnabled &&
            std::chrono::steady_clock::now() - lastRefreshCheck > std::chrono::seconds(10))
        {
            lastRefreshCheck = std::chrono::steady_clock::now();
            priceCache_.RefreshIfNeeded();

            if (priceCache_.Version() != cachedNamesVersion_)
                RebuildCachedNames();
        }

        const bool snapshotRequested = singleSnapshotRequested_.exchange(false);

        if (snapshotRequested)
            singleSnapshotUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        const bool keepSnapshot = std::chrono::steady_clock::now() < singleSnapshotUntil_;
        bool calibrationRunning = runeMatcher_.CalibrationStatus().running;

        if (!config.ocrEnabled && !snapshotRequested && !keepSnapshot && !calibrationRunning)
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

        calibrationRunning = ProcessFrame(region, config, calibrationRunning);

        runeCalibrationWasRunning_ = calibrationRunning;

        SleepOcrLoop(running_, singleSnapshotRequested_, NextSleepMs(config));
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
    cachedNamesVersion_ = 0;
    forceOcrFrame_ = false;
    runeCalibrationWasRunning_ = false;
    ResetFrameState();
    ClearRuntimeBuffers();
}

void OcrService::ClearRuntimeBuffers()
{
    {
        std::lock_guard lock(overlayMutex_);
        sharedTexts_.clear();
    }

    {
        std::lock_guard lock(debugMutex_);
        debugData_ = {};
    }

    {
        std::lock_guard lock(cachedNamesMutex_);
        cachedItemNames_.reset();
    }
}

void OcrService::ClearOverlayTexts()
{
    emptyOverlayFrames_ = 0;
    SetOverlayTexts({});
}

void OcrService::SaveRuneCalibrationScale(double scale)
{
    if (!configManager_)
        return;

    if (configManager_->Snapshot().runeSearchScale == scale)
        return;

    configManager_->Update(
        [scale](AppConfig& config)
        {
            config.runeSearchScale = scale;
        });

    if (!configManager_->Save())
        LOG_ERROR("OcrService failed to save rune calibration scale");
}

void OcrService::SetOverlayTexts(std::vector<OverlayText> texts)
{
    std::lock_guard lock(overlayMutex_);

    if (EqualOverlayTexts(sharedTexts_, texts))
        return;

    sharedTexts_ = std::move(texts);
    overlayDirty_ = true;
}

void OcrService::PublishOverlayTexts(std::vector<OverlayText> texts)
{
    if (!texts.empty())
    {
        emptyOverlayFrames_ = 0;
        SetOverlayTexts(std::move(texts));
        return;
    }

    ++emptyOverlayFrames_;

    if (emptyOverlayFrames_ >= kEmptyOverlayFramesBeforeClear)
        SetOverlayTexts({});
}
