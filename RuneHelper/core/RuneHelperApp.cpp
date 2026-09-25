#include "RuneHelperApp.h"

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>

#include "core/DebugData.h"
#include "core/Logger.h"
#include "features/ExpeditionFeature.h"
#include "features/PriceOverlayFeature.h"

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
