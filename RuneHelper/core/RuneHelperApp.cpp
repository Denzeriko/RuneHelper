#include "RuneHelperApp.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "core/DebugData.h"
#include "features/ExpeditionFeature.h"
#include "features/PriceOverlayFeature.h"
#include "core/Logger.h"

#include <opencv2/core.hpp>

#ifdef _WIN32
#include "platform/windows/RegionSelect.h"
#else
#include "platform/linux/RegionSelect.h"
#endif

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

    configManager_.Load();

    if (!ui_.Init(&configManager_))
        return false;

    const bool overlayAvailable = overlay_.Create();

    if (overlayAvailable)
        overlay_.SetFontSizeForce(configManager_.Snapshot().overlayFontSize);
    else
        LOG_ERROR("Overlay is unavailable, RuneHelper will run without it");

    ui_.SetOverlayAvailable(overlayAvailable);

    updateChecker_.Start();
    ui_.SetUpdateChecker(&updateChecker_);

    features_.Add(std::make_unique<PriceOverlayFeature>());
    features_.Add(std::make_unique<ExpeditionFeature>());
    features_.InitAll(configManager_);

    ui_.SetFeatures(&features_);

    ui_.RegisterHotkeys();

    prices_.Apply(configManager_.Snapshot());

    ocrService_.Start(configManager_, features_, prices_);

    return true;
}

void RuneHelperApp::MainLoop()
{
    while (ui_.IsRunning())
    {
        OcrServiceStatus ocrStatus = ocrService_.GetStatus();
        ui_.SetStatus(ocrStatus.initializing, ocrStatus.ready, ocrStatus.failed);
        ui_.SetCaptureFailing(ocrStatus.captureFailing);

        const PriceStatus priceStatus = prices_.Status();
        ui_.SetPriceStatus(priceStatus.downloading, priceStatus.priceCount);

        if (ui_.NeedsDebugData())
        {
            DebugData debugData;

            if (ocrService_.ConsumeDebugData(debugData))
                ui_.SetDebugData(std::move(debugData));
        }

        ui_.Pump();
        overlay_.PumpMessages();

        HandleUIActions();

        const AppConfig config = configManager_.Snapshot();

        prices_.Tick(config);

        UpdateRegionPreview(config);

        UpdateOverlay();

        overlay_.SetFontSize(config.overlayFontSize);

        static auto lastTop = std::chrono::steady_clock::now();

        auto now = std::chrono::steady_clock::now();

        if (now - lastTop > std::chrono::seconds(2))
        {
            overlay_.BringToTop();
            lastTop = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

void RuneHelperApp::HandleUIActions()
{
    if (ui_.WantsToggleOCR())
    {
        configManager_.Update(
            [](AppConfig& config)
            {
                config.ocrEnabled = !config.ocrEnabled;
            });

        configManager_.Save();
    }

    if (ui_.WantsSingleSnapshot())
        ocrService_.RequestSingleSnapshot();

    if (ui_.WantsRefreshPrices())
        prices_.ForceRefresh();

    if (ui_.WantsSelectRegion())
    {
        RegionSelector selector;

        cv::Rect newRegion = selector.Select();

        constexpr int kMinRegionSide = 16;

        if (newRegion.width >= kMinRegionSide && newRegion.height >= kMinRegionSide)
        {
            configManager_.Update(
                [&newRegion](AppConfig& config)
                {
                    config.regionX = newRegion.x;
                    config.regionY = newRegion.y;
                    config.regionW = newRegion.width;
                    config.regionH = newRegion.height;
                });

            configManager_.Save();
            features_.NotifyRegionChanged();
        }
    }

    if (ui_.WantsRegisterHotkeys())
        ui_.RegisterHotkeys();
}

void RuneHelperApp::UpdateOverlay()
{
    OverlayFrame frame;

    if (!ocrService_.ConsumeOverlayFrame(frame))
        return;

    overlay_.SetFrame(std::move(frame));
}

void RuneHelperApp::UpdateRegionPreview(const AppConfig& localConfig)
{
    if (!ui_.IsRegionHovered() || localConfig.regionW <= 0)
    {
        static OverlayRect empty{};
        overlay_.SetRegionPreview(false, empty);
        return;
    }

    OverlayRect rect{
        localConfig.regionX,
        localConfig.regionY,
        localConfig.regionX + localConfig.regionW,
        localConfig.regionY + localConfig.regionH
    };

    overlay_.SetRegionPreview(true, rect);
}

void RuneHelperApp::Shutdown()
{
    ocrService_.Stop();
    ui_.UnregisterHotkeys();
    updateChecker_.Stop();
    features_.ShutdownAll();
}
