#include "core/OcrService.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

#include "common/ExceptionLogging.h"
#include "common/Logger.h"
#include "platform/GameFocus.h"

namespace
{
constexpr int kPollIntervalMs = 100;
constexpr int kMinOcrGapMs = 600;
constexpr int kMaxOcrDelayMs = 1500;
constexpr int kEmptyOverlayFramesBeforeClear = 3;
constexpr int kCaptureFailuresBeforeWarning = 3;
constexpr int kOverlayYJitter = 3;
constexpr std::chrono::seconds kSnapshotDuration{ 2 };

}

OcrService::OcrService(
    ConfigManager& configManager,
    FeatureRegistry& features,
    PriceService& prices,
    std::unique_ptr<IScreenCapture> screenCapture
)
    : configManager_(configManager), pipeline_(features, prices),
      screenCapture_(screenCapture ? std::move(screenCapture) : CreateScreenCapture())
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

    workerThread_ = std::jthread([this](const std::stop_token& stop)
                                 { RunLoggingExceptions("OcrService worker thread", [&] { WorkerLoop(stop); }); });
}

void OcrService::Stop()
{
    if (!running_.exchange(false) && !workerThread_.joinable())
        return;

    workerThread_.request_stop();
    screenCapture_->Cancel();
    commandCondition_.notify_all();

    if (workerThread_.joinable())
        workerThread_.join();

    screenCapture_->Shutdown();
    ResetState(OcrState::Stopped);
}

void OcrService::RequestSingleSnapshot()
{
    Enqueue(Command::SingleSnapshot);
}

void OcrService::RequestDebugDump()
{
    Enqueue(Command::DebugDump);
}

void OcrService::Enqueue(Command command)
{
    if (!running_.load())
        return;

    {
        std::lock_guard lock(commandMutex_);
        if (!running_.load())
            return;
        if (std::find(commands_.begin(), commands_.end(), command) == commands_.end())
            commands_.push_back(command);
    }

    commandCondition_.notify_one();
}

bool OcrService::DrainCommands()
{
    std::deque<Command> commands;

    {
        std::lock_guard lock(commandMutex_);
        commands.swap(commands_);
    }

    bool snapshotRequested = false;

    for (const Command command : commands)
    {
        snapshotRequested = true;
        forceOcr_ = true;

        if (command == Command::DebugDump)
            debugDumpRequested_ = true;
    }

    return snapshotRequested;
}

void OcrService::WaitForWork(int milliseconds)
{
    std::unique_lock lock(commandMutex_);
    commandCondition_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return !running_ || !commands_.empty(); });
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
    language_ = configManager_.Snapshot().gameLanguage;
    RunLoggingExceptions("OcrService init", [&] { loaded = pipeline_.LoadLanguage(language_); });

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

void OcrService::ProcessFrame(const cv::Rect& region, const AppConfig& config, const std::stop_token& stop)
{
    cv::Mat gray = screenCapture_->CaptureRegion(region, stop);

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
        const bool dump = std::exchange(debugDumpRequested_, false);
        lastLoot_ = pipeline_.RecognizeLoot(gray, rowCache_, dump);
        frameDiffer_.StoreOcrFrame(gray);
        lastOcrAt_ = std::chrono::steady_clock::now();

        if (dump)
            ++debugDumpsWritten_;
    }

    OcrPipelineResult result = pipeline_.BuildFrame(lastLoot_, gray, region, config, rowCache_);
    PublishOverlayFrame(std::move(result.overlay));

    {
        std::lock_guard lock(debugMutex_);
        debugData_ = std::move(result.debug);
    }

    debugDirty_ = true;

    frameDiffer_.StoreFrame(std::move(gray));
}

bool OcrService::NeedsOcr(const cv::Mat& gray)
{
    if (forceOcr_)
    {
        forceOcr_ = false;
        return true;
    }

    if (!frameDiffer_.ChangedSinceOcr(gray))
        return false;

    const auto sinceOcr = std::chrono::steady_clock::now() - lastOcrAt_;

    if (sinceOcr < std::chrono::milliseconds(kMinOcrGapMs))
        return false;

    return frameDiffer_.IsSettled(gray) || sinceOcr >= std::chrono::milliseconds(kMaxOcrDelayMs);
}

void OcrService::WorkerLoop(const std::stop_token& stop)
{
    if (!InitOcr())
        return;

    while (running_ && !stop.stop_requested())
    {
        const AppConfig config = configManager_.Snapshot();

        const bool snapshotRequested = DrainCommands();

        if (snapshotRequested)
            singleSnapshotUntil_ = std::chrono::steady_clock::now() + kSnapshotDuration;

        const bool keepSnapshot = std::chrono::steady_clock::now() < singleSnapshotUntil_;
        if (!config.ocrEnabled && !snapshotRequested && !keepSnapshot)
        {
            waitingForGame_ = false;
            ResetFrameState();
            ClearOverlayTexts();
            WaitForWork(kPollIntervalMs);

            continue;
        }

        if (config.regionW <= 0 || config.regionH <= 0)
        {
            waitingForGame_ = false;
            ResetFrameState();
            WaitForWork(kPollIntervalMs);

            continue;
        }

        if (PauseForGame(config, snapshotRequested || keepSnapshot))
        {
            WaitForWork(kPollIntervalMs);

            continue;
        }

        if (config.gameLanguage != language_)
        {
            pipeline_.LoadLanguage(config.gameLanguage);
            language_ = config.gameLanguage;
            ResetFrameState();
        }

        const cv::Rect region(config.regionX, config.regionY, config.regionW, config.regionH);

        try
        {
            ProcessFrame(region, config, stop);
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

        WaitForWork(kPollIntervalMs);
    }
}

void OcrService::ResetState(OcrState state)
{
    state_ = state;
    waitingForGame_ = false;
    {
        std::lock_guard lock(commandMutex_);
        commands_.clear();
    }
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
