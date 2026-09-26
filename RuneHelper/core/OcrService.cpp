#include "core/OcrService.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "core/ExceptionLogging.h"
#include "core/Logger.h"
#include "ocr/LootRows.h"
#include "platform/GameFocus.h"
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
constexpr int kOverlayYJitter = 3;
constexpr int kOverlayRowSpacing = 25;
constexpr std::chrono::seconds kSnapshotDuration{ 2 };

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

OcrService::OcrService(ConfigManager& configManager, FeatureRegistry& features, PriceService& prices)
    : configManager_(configManager), features_(features), prices_(prices)
{
}

OcrService::~OcrService()
{
    Stop();
}

void OcrService::Start()
{
    if (running_.exchange(true))
    {
        LOG_INFO("OcrService::Start() ignored because service is already running");
        return;
    }

    ResetState(OcrState::Initializing);

    workerThread_ = std::jthread([this] { RunLoggingExceptions("OcrService worker thread", [this] { WorkerLoop(); }); });
}

void OcrService::Stop()
{
    if (!running_.exchange(false) && !workerThread_.joinable())
        return;

    screenCapture_.Cancel();

    if (workerThread_.joinable())
        workerThread_.join();

    screenCapture_.Shutdown();
    ResetState(OcrState::Stopped);
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

OcrStatus OcrService::Status() const
{
    return { state_.load(), captureFailing_.load(), waitingForGame_.load() };
}

unsigned OcrService::DebugDumpsWritten() const
{
    return debugDumpsWritten_.load();
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

bool OcrService::InitOcr()
{
    LOG_INFO("Initializing OCR");

    bool loaded = false;
    RunLoggingExceptions("OcrService init", [&] { loaded = LoadLanguage(configManager_.Snapshot().gameLanguage); });

    if (!loaded)
    {
        LOG_ERROR("OCR init failed");
        state_ = OcrState::Failed;
        return false;
    }

    state_ = OcrState::Ready;
    LOG_INFO("OCR ready");
    return true;
}

bool OcrService::LoadLanguage(const std::string& language)
{
    language_ = language;
    translations_ = NameMatcher();

    std::string_view model = EmbeddedTextModel(language);

    if (model.empty())
    {
        LOG_ERROR("OCR: this build has no text model for game language '" + language + "', English text is read instead");
        model = EmbeddedTextModel("en");
    }

    if (!ocr_.Init(model))
        return false;

    const bool recipesLoaded = recipes_.Load();

    if (language == "en")
        return true;

    if (recipesLoaded)
        translations_ = recipes_.Translations(language);

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

void OcrService::PublishFrameResult(
    const std::vector<LootLine>& loot,
    const cv::Mat& gray,
    const cv::Rect& region,
    const AppConfig& config
)
{
    std::vector<FrameRow> rows = ParseLootRows(loot, region, config, translations_.Empty() ? nullptr : &translations_);

    const bool pricesLoaded = config.priceSearchEnabled && prices_.Status().priceCount > 0;

    for (FrameRow& row : rows)
    {
        if (config.priceSearchEnabled)
            row.price = prices_.Resolve(row.name, row.quantity);

        row.missingPrice =
            pricesLoaded && !row.price.unitEx && recipes_.Loaded() && recipes_.FindRecipe(row.name, row.quantity) != nullptr;
    }

    DebugData debug;
    debug.lines.reserve(loot.size());

    for (const auto& item : loot)
    {
        DebugLine line;
        line.ocrText = item.text;
        debug.lines.push_back(std::move(line));
    }

    std::vector<RowOverlay> rowOverlays(rows.size());
    OverlayFrame overlay;

    FrameContext frame{
        gray,
        region,
        rows,
        config,
        prices_.DivineRate(),
        rowOverlays,
        overlay,
        debug,
        rowCache_.Panel().value_or(cv::Rect(0, 0, gray.cols, gray.rows)),
        rowCache_.Levels(),
    };

    features_.RunFrame(frame);

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

bool OcrService::PauseForGame(const AppConfig& config, bool snapshot)
{
    const bool pause = config.pauseWhenGameInactive && !snapshot && QueryGameFocus() == GameFocus::Inactive;

    if (pause == waitingForGame_.exchange(pause))
        return pause;

    if (pause)
    {
        ClearOverlayTexts();
        LOG_INFO("OCR paused: Path of Exile is not the active window");
    }
    else
    {
        forceOcr_ = true;
        LOG_INFO("OCR resumed: Path of Exile is the active window again");
    }

    return pause;
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
        const bool dump = debugDumpRequested_.exchange(false);
        lastLoot_ = ocr_.RecognizeLoot(gray, &rowCache_, dump);
        frameDiffer_.StoreOcrFrame(gray);
        lastOcrAt_ = std::chrono::steady_clock::now();

        if (dump)
            ++debugDumpsWritten_;
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
    if (!InitOcr())
        return;

    while (running_)
    {
        const AppConfig config = configManager_.Snapshot();

        const bool snapshotRequested = singleSnapshotRequested_.exchange(false);

        if (snapshotRequested)
            singleSnapshotUntil_ = std::chrono::steady_clock::now() + kSnapshotDuration;

        const bool keepSnapshot = std::chrono::steady_clock::now() < singleSnapshotUntil_;
        if (!config.ocrEnabled && !snapshotRequested && !keepSnapshot)
        {
            waitingForGame_ = false;
            ResetFrameState();
            ClearOverlayTexts();
            SleepOcrLoop(running_, singleSnapshotRequested_, kPollIntervalMs);

            continue;
        }

        if (config.regionW <= 0 || config.regionH <= 0)
        {
            waitingForGame_ = false;
            ResetFrameState();
            SleepOcrLoop(running_, singleSnapshotRequested_, kPollIntervalMs);

            continue;
        }

        if (PauseForGame(config, snapshotRequested || keepSnapshot))
        {
            SleepOcrLoop(running_, singleSnapshotRequested_, kPollIntervalMs);

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

void OcrService::ResetState(OcrState state)
{
    state_ = state;
    waitingForGame_ = false;
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
