#include "RuneHelperApp.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>

#include "core/BugReport.h"
#include "core/DebugData.h"
#include "core/Logger.h"
#include "features/ExpeditionFeature.h"
#include "features/PriceOverlayFeature.h"
#include "platform/GameFocus.h"
#include "platform/PlatformPaths.h"
#include "platform/PlatformShell.h"

#ifdef _WIN32
#include "platform/windows/RegionSelect.h"
#else
#include "platform/linux/RegionSelect.h"
#endif

namespace
{
constexpr int kMinRegionSide = 16;
constexpr std::chrono::milliseconds kFrameInterval{ 33 };
constexpr std::chrono::seconds kBringToTopInterval{ 2 };
constexpr std::chrono::seconds kReportDumpWait{ 5 };

const char* OcrStateName(OcrState state)
{
    switch (state)
    {
    case OcrState::Initializing: return "initializing";
    case OcrState::Failed: return "failed";
    case OcrState::Ready: return "ready";
    case OcrState::Stopped: return "stopped";
    }

    return "unknown";
}
}

int RuneHelperApp::Run()
{
    if (!Init())
        return 1;

    MainLoop();

    Shutdown();

    return 0;
}

bool RuneHelperApp::Init()
{
    Logger::Instance().Init();

    LOG_INFO("--------------------------");
    LOG_INFO("RuneHelper started! v" RUNEHELPER_VERSION);

    cv::setNumThreads(1);

    configManager_.Load();

    if (!ui_.Init())
        return false;

    const bool overlayAvailable = overlay_.Create();

    if (overlayAvailable)
        overlay_.SetFontSize(configManager_.Snapshot().overlayFontSize);
    else
        LOG_ERROR("Overlay is unavailable, RuneHelper will run without it");

    ui_.State().overlayAvailable = overlayAvailable;

    updateChecker_.Start();

    features_.Add(std::make_unique<PriceOverlayFeature>());
    features_.Add(std::make_unique<ExpeditionFeature>());
    features_.InitAll(configManager_);

    ui_.RegisterHotkeys();

    prices_.Apply(configManager_.Snapshot());

    ocrService_.Start();

    return true;
}

void RuneHelperApp::MainLoop()
{
    auto lastTop = std::chrono::steady_clock::now();

    while (ui_.IsRunning())
    {
        PublishStatus();

        ui_.Pump();
        overlay_.PumpMessages();

        HandleRequests(ui_.TakeRequests());
        FinishBugReport();
        configManager_.SaveIfSettled();

        const AppConfig config = configManager_.Snapshot();

        prices_.Tick(config);

        UpdateRegionPreview(config);

        UpdateOverlay();

        overlay_.SetFontSize(config.overlayFontSize);
        overlay_.SetBackground(config.overlayBackground);
        overlay_.SetOutline(config.overlayOutline);

        const auto now = std::chrono::steady_clock::now();

        if (now - lastTop > kBringToTopInterval)
        {
            overlay_.BringToTop();
            lastTop = now;
        }

        std::this_thread::sleep_for(kFrameInterval);
    }
}

void RuneHelperApp::PublishStatus()
{
    UIState& state = ui_.State();
    state.ocr = ocrService_.Status();

    const PriceStatus priceStatus = prices_.Status();
    state.priceDownloading = priceStatus.downloading;
    state.priceCount = priceStatus.priceCount;

    if (!ui_.NeedsDebugData())
        return;

    DebugData debugData;

    if (ocrService_.ConsumeDebugData(debugData))
        ui_.SetDebugData(std::move(debugData));
}

void RuneHelperApp::HandleRequests(const UIRequests& requests)
{
    if (requests.toggleOcr)
        configManager_.Update([](AppConfig& config) { config.ocrEnabled = !config.ocrEnabled; });

    if (requests.singleSnapshot)
        ocrService_.RequestSingleSnapshot();

    if (requests.saveOcrDebug)
        ocrService_.RequestDebugDump();

    if (requests.createReport)
        StartBugReport();

    if (requests.refreshPrices)
        prices_.ForceRefresh();

    if (requests.selectRegion)
        SelectRegion();

    if (requests.registerHotkeys)
        ui_.RegisterHotkeys();
}

void RuneHelperApp::SelectRegion()
{
    RegionSelector selector;

    const cv::Rect region = selector.Select();

    if (region.width < kMinRegionSide || region.height < kMinRegionSide)
        return;

    configManager_.Update(
        [&region](AppConfig& config)
        {
            config.regionX = region.x;
            config.regionY = region.y;
            config.regionW = region.width;
            config.regionH = region.height;
        }
    );

    features_.NotifyRegionChanged();
}

void RuneHelperApp::StartBugReport()
{
    if (reportPending_)
        return;

    reportPending_ = true;
    reportDumps_ = ocrService_.DebugDumpsWritten();
    reportStarted_ = std::chrono::steady_clock::now();

    ocrService_.RequestDebugDump();
    ui_.State().report = ReportState::Collecting;
}

void RuneHelperApp::FinishBugReport()
{
    if (!reportPending_)
        return;

    const bool freshDump = ocrService_.DebugDumpsWritten() != reportDumps_;

    if (!freshDump && std::chrono::steady_clock::now() - reportStarted_ < kReportDumpWait)
        return;

    reportPending_ = false;
    configManager_.Flush();

    const std::optional<std::filesystem::path> report = WriteBugReport(DescribeRun(freshDump));
    UIState& state = ui_.State();

    state.report = report ? ReportState::Saved : ReportState::Failed;
    state.reportFolder = report ? PathToUtf8(report->parent_path()) : std::string();
}

std::string RuneHelperApp::DescribeRun(bool freshDump)
{
    const OcrStatus ocr = ocrService_.Status();
    const PriceStatus priceStatus = prices_.Status();

    std::string text = "RuneHelper " RUNEHELPER_VERSION_LABEL ", " RUNEHELPER_BUILD_VARIANT " build\n";
    text += DescribeSystem();
    text += std::string("OCR: ") + OcrStateName(ocr.state) + (ocr.captureFailing ? ", capture failing" : ", capture OK") +
            (ocr.waitingForGame ? ", paused while the game is not active" : "") + "\n";
    text += std::string("Active window check: ") + (GameFocusSupported() ? "yes" : "no") + "\n";
    text += std::string("Overlay: ") + (ui_.State().overlayAvailable ? "ready" : "unavailable") + "\n";
    text += "Prices: " + std::to_string(priceStatus.priceCount) + " items" + (priceStatus.downloading ? ", downloading" : "") + "\n";
    text += freshDump ? "OCR debug: read for this report\n" : "OCR debug: the region was not read, older files if any\n";

    return text;
}

void RuneHelperApp::UpdateOverlay()
{
    OverlayFrame frame;

    if (!ocrService_.ConsumeOverlayFrame(frame))
        return;

    overlay_.SetFrame(std::move(frame));
}

void RuneHelperApp::UpdateRegionPreview(const AppConfig& config)
{
    if (!ui_.State().regionHovered || config.regionW <= 0)
    {
        overlay_.SetRegionPreview(false, OverlayRect{});
        return;
    }

    const OverlayRect rect{ config.regionX, config.regionY, config.regionX + config.regionW, config.regionY + config.regionH };

    overlay_.SetRegionPreview(true, rect);
}

void RuneHelperApp::Shutdown()
{
    ocrService_.Stop();
    ui_.UnregisterHotkeys();
    updateChecker_.Stop();
    features_.ShutdownAll();
    configManager_.Flush();
}
