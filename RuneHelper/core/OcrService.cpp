#include "core/OcrService.h"

#include <algorithm>
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
    ResetRuntimeState();

    AppConfig config;
    {
        std::lock_guard lock(configManager.Mutex());
        config = configManager.Get();
    }

    priceCache_.SetRefreshMinutes(config.priceRefreshMinutes);
    priceCache_.SetLeague(config.priceLeague);
    PrepareRuneTemplates();
    SetRunePatternSearchScale(config.runeSearchScale);
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
    ResetStoppedState();
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

    BeginRunePatternScaleCalibration();
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
        ocrFailed_.load()
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
    return GetRunePatternCalibrationStatus();
}

DebugData OcrService::GetDebugData()
{
    std::lock_guard lock(debugMutex_);
    return debugData_;
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

void OcrService::WorkerLoop()
{
    while (running_ && !ocrReady_)
    {
        if (ocrFailed_)
            return;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    {
        std::lock_guard lock(cachedNamesMutex_);
        cachedItemNames_ = BuildCachedItemNames(priceCache_.GetAllItemNames());
    }

    auto lastRefreshCheck = std::chrono::steady_clock::now();
    std::vector<LootLine> lastLoot;
    std::vector<RunePatternMatch> lastRunes;
    bool lastRunesValid = false;
    bool forceOcrFrame = false;
    bool runeCalibrationWasRunning = false;

    while (running_)
    {
        if (!configManager_)
            return;

        AppConfig localConfig;
        {
            std::lock_guard lock(configManager_->Mutex());
            localConfig = configManager_->Get();
        }

        priceCache_.SetRefreshMinutes(localConfig.priceRefreshMinutes);
        priceCache_.SetLeague(localConfig.priceLeague);

        if (localConfig.priceSearchEnabled &&
            std::chrono::steady_clock::now() - lastRefreshCheck > std::chrono::seconds(10))
        {
            lastRefreshCheck = std::chrono::steady_clock::now();
            priceCache_.RefreshIfNeeded();
            {
                std::lock_guard lock(cachedNamesMutex_);
                cachedItemNames_ = BuildCachedItemNames(priceCache_.GetAllItemNames());
            }
        }

        bool runSingleSnapshot = singleSnapshotRequested_.exchange(false);

        if (runSingleSnapshot)
            singleSnapshotUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        bool keepSnapshot = std::chrono::steady_clock::now() < singleSnapshotUntil_;
        RunePatternCalibrationStatus runeCalibrationStatus = GetRunePatternCalibrationStatus();
        bool runeCalibrationRunning = runeCalibrationStatus.running;

        if (!localConfig.ocrEnabled && !runSingleSnapshot && !keepSnapshot && !runeCalibrationRunning)
        {
            frameDiffer_.Reset();
            lastLoot.clear();
            lastRunes.clear();
            lastRunesValid = false;
            ClearOverlayTexts();
            SleepOcrLoop(running_, singleSnapshotRequested_, 100);
            continue;
        }

        if (localConfig.regionW <= 0 || localConfig.regionH <= 0)
        {
            frameDiffer_.Reset();
            lastLoot.clear();
            lastRunes.clear();
            lastRunesValid = false;
            SleepOcrLoop(running_, singleSnapshotRequested_, 100);
            continue;
        }

        cv::Rect localRegion(
            localConfig.regionX,
            localConfig.regionY,
            localConfig.regionW,
            localConfig.regionH
        );

        cv::Mat img = screenCapture_.CaptureRegion(localRegion);

        if (!img.empty())
        {
            StepRunePatternScaleCalibration(img);
            runeCalibrationStatus = GetRunePatternCalibrationStatus();
            runeCalibrationRunning = runeCalibrationStatus.running;

            if (runeCalibrationWasRunning && !runeCalibrationStatus.running && runeCalibrationStatus.bestScale > 0.0)
                SaveRuneCalibrationScale(runeCalibrationStatus.bestScale);

            std::vector<LootLine> loot;
            const bool similarFrame = frameDiffer_.IsSimilarFrame(img, forceOcrFrame);
            const bool stableFrame = similarFrame && frameDiffer_.StableFrames() >= kStableOcrFramesBeforeReuse;

            if (stableFrame && !lastLoot.empty())
            {
                loot = lastLoot;
            }
            else
            {
                loot = ocr_.RecognizeLoot(img, localConfig);
                lastLoot = loot;
                forceOcrFrame = false;
            }

            frameDiffer_.StoreFrame(img);

            std::vector<CachedItemName> cachedNames;
            {
                std::lock_guard lock(cachedNamesMutex_);
                cachedNames = cachedItemNames_;
            }

            LootOverlayBuildResult buildResult = LootOverlayBuilder::Build(
                loot,
                localRegion,
                localConfig,
                priceCache_,
                cachedNames
            );

            if (localConfig.runeSearchEnabled && !runeCalibrationRunning)
            {
                if (!stableFrame || !lastRunesValid)
                {
                    lastRunes = FindRunePatternMatches(img);
                    lastRunesValid = true;
                }

                for (const auto& runeMatch : lastRunes)
                {
                    OverlayText text;
                    text.text = std::wstring(runeMatch.label.begin(), runeMatch.label.end());
                    text.color = OverlayRgb(60, 255, 60);
                    text.x = localRegion.x + runeMatch.rect.x + runeMatch.rect.width / 2 - localConfig.overlayFontSize / 4;
                    text.y = localRegion.y + runeMatch.rect.y + runeMatch.rect.height / 2;
                    buildResult.texts.push_back(std::move(text));
                }
            }
            else
            {
                lastRunes.clear();
                lastRunesValid = false;
            }

            PublishOverlayTexts(std::move(buildResult.texts));

            {
                std::lock_guard lock(debugMutex_);
                debugData_ = std::move(buildResult.debug);
            }
        }

        int sleepMs = localConfig.ocrIntervalMs;
        if (frameDiffer_.StableFrames() >= kStableOcrFramesBeforeReuse)
            sleepMs = std::min(localConfig.ocrIntervalMs * 2, kMaxStableOcrIntervalMs);

        runeCalibrationWasRunning = runeCalibrationRunning;

        SleepOcrLoop(running_, singleSnapshotRequested_, sleepMs);
    }
}

void OcrService::ResetRuntimeState()
{
    ocrReady_ = false;
    ocrFailed_ = false;
    ocrInitializing_ = true;
    singleSnapshotRequested_ = false;
    singleSnapshotUntil_ = {};
    overlayDirty_ = false;
    emptyOverlayFrames_ = 0;
    frameDiffer_.Reset();
    ClearRuntimeBuffers();
}

void OcrService::ResetStoppedState()
{
    ocrReady_ = false;
    ocrFailed_ = false;
    ocrInitializing_ = false;
    singleSnapshotRequested_ = false;
    singleSnapshotUntil_ = {};
    overlayDirty_ = false;
    emptyOverlayFrames_ = 0;
    frameDiffer_.Reset();
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
        cachedItemNames_.clear();
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

    std::lock_guard lock(configManager_->Mutex());
    AppConfig& config = configManager_->Get();

    if (config.runeSearchScale == scale)
        return;

    config.runeSearchScale = scale;
    ConfigManager::Normalize(config);

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
