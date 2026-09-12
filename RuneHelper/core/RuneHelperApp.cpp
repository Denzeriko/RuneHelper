#include "RuneHelperApp.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

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

    config_ = &configManager_.Get();

    if (!ui_.Init(config_, &configManager_))
        return false;

    if (!overlay_.Create())
        return false;

    overlay_.SetFontSizeForce(config_->overlayFontSize);

    updateChecker_.Start();
    ui_.SetUpdateChecker(&updateChecker_);

    ui_.RegisterHotkeys();

    ocrService_.Start(configManager_);

    return true;
}

void RuneHelperApp::MainLoop()
{
    while (ui_.IsRunning())
    {
        OcrServiceStatus ocrStatus = ocrService_.GetStatus();
        ui_.SetStatus(ocrStatus.initializing, ocrStatus.ready, ocrStatus.failed);

        PriceServiceStatus priceStatus = ocrService_.GetPriceStatus();
        ui_.SetPriceStatus(priceStatus.downloading, priceStatus.priceCount);
        ui_.SetRuneCalibrationStatus(ocrService_.GetRuneCalibrationStatus());
        ui_.SetDebugData(ocrService_.GetDebugData());

        ui_.Pump();
        overlay_.PumpMessages();

        HandleUIActions();

        UpdateRegionPreview();

        UpdateOverlay();

        int overlayFontSize = 0;
        {
            std::lock_guard lock(configManager_.Mutex());
            overlayFontSize = config_->overlayFontSize;
        }

        overlay_.SetFontSize(overlayFontSize);

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
        std::lock_guard lock(configManager_.Mutex());
        config_->ocrEnabled = !config_->ocrEnabled;
        configManager_.Save();
    }

    if (ui_.WantsSingleSnapshot())
        ocrService_.RequestSingleSnapshot();

    if (ui_.WantsRefreshPrices())
        ocrService_.ForceRefreshPrices();

    if (ui_.WantsCalibrateRunes())
        ocrService_.RequestRuneCalibration();

    if (ui_.WantsSelectRegion())
    {
        RegionSelector selector;

        cv::Rect newRegion = selector.Select();

        if (!newRegion.empty())
        {
            std::lock_guard lock(configManager_.Mutex());
            config_->regionX = newRegion.x;
            config_->regionY = newRegion.y;
            config_->regionW = newRegion.width;
            config_->regionH = newRegion.height;

            configManager_.Save();
        }
    }

    if (ui_.WantsRegisterHotkeys())
        ui_.RegisterHotkeys();
}

void RuneHelperApp::UpdateOverlay()
{
    std::vector<OverlayText> texts;
    if (!ocrService_.ConsumeOverlayTexts(texts))
        return;

    overlay_.SetTexts(std::move(texts));
}

void RuneHelperApp::UpdateRegionPreview()
{
    AppConfig localConfig;
    {
        std::lock_guard lock(configManager_.Mutex());
        localConfig = *config_;
    }

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
}
