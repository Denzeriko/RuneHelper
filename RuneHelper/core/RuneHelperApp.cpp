#include "RuneHelperApp.h"

#include <chrono>

#include "core/Helpers.h"
#include "core/Logger.h"

#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"

#include "platform/windows/RegionSelect.h"
#include "platform/windows/ResourceHelper.h"
#include "platform/windows/ScreenCapture.h"

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
    LOG_INFO("RuneHelper started!");

    configManager_.Load();

    config_ = &configManager_.Get();

    if (config_->regionW > 0 && config_->regionH > 0)
    {
        region_ = cv::Rect(
            config_->regionX,
            config_->regionY,
            config_->regionW,
            config_->regionH
        );
    }

    if (!ui_.Init(config_, &configManager_))
        return false;

    if (!overlay_.Create())
        return false;

    overlay_.SetFontSizeForce(config_->overlayFontSize);

    updateChecker_.Start();
    ui_.SetUpdateChecker(&updateChecker_);

    ocr_.SetConfig(config_);

    initThread_ = std::jthread(
        [this]
        {
            InitOcr();
        });

    ocrThread_ = std::jthread(
        [this]
        {
            OcrWorkerLoop();
        });

    return true;
}

void RuneHelperApp::InitOcr()
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

void RuneHelperApp::OcrWorkerLoop()
{
    while (running_ && !ocrReady_)
    {
        if (ocrFailed_)
            return;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto lastRefreshCheck = std::chrono::steady_clock::now();

    while (running_)
    {
        if (std::chrono::steady_clock::now() - lastRefreshCheck > std::chrono::seconds(10))
        {
            lastRefreshCheck = std::chrono::steady_clock::now();
            priceCache_.RefreshIfNeeded();
        }

        if (!config_->ocrEnabled)
        {
            {
                std::lock_guard lock(overlayMutex_);
                sharedTexts_.clear();
                overlayDirty_ = true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (region_.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        cv::Rect localRegion(
            config_->regionX,
            config_->regionY,
            config_->regionW,
            config_->regionH
        );

        cv::Mat img = CaptureRegion(localRegion);

        if (!img.empty())
        {
            auto loot = ocr_.RecognizeLoot(img);

            std::vector<OverlayText> newTexts;

            if ((loot.size() < 2) && config_->ocrAutoDetect)
            {
                {
                    std::lock_guard lock(overlayMutex_);
                    sharedTexts_.clear();
                }

                overlayDirty_ = true;

                std::this_thread::sleep_for(std::chrono::milliseconds(config_->ocrIntervalMs));

                continue;
            }

            for (const auto& item : loot)
            {
                auto parsed = LootParser::ParseLootLine(item.text);

                std::string rawName = parsed.itemName;
                int quantity = parsed.quantity;

                auto price = priceCache_.GetPrice(rawName);

                if (!price)
                {
                    auto guess = FindBestItemMatch(rawName, priceCache_.GetAllItemNames());

                    if (guess)
                        price = priceCache_.GetPrice(*guess);
                }

                if (!price)
                    continue;

                std::optional<double> value = LootParser::ParsePriceValue(*price);

                double totalValue = value ? (*value * quantity) : 0.0;

                OverlayText t;

                t.color = GetPriceColor(totalValue, *config_);
                t.text = ToWide(LootParser::FormatStackPrice(*price, quantity));
                t.x = localRegion.x + localRegion.width + config_->overlayOffsetX;
                t.y = localRegion.y + (item.y1 + item.y2) / 2 + config_->overlayOffsetY;

                newTexts.push_back(std::move(t));
            }

            {
                std::lock_guard lock(overlayMutex_);
                sharedTexts_ = std::move(newTexts);
                overlayDirty_ = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config_->ocrIntervalMs));
    }
}

void RuneHelperApp::MainLoop()
{
    while (ui_.IsRunning())
    {
        ui_.SetStatus(ocrInitializing_, ocrReady_, ocrFailed_);
        ui_.SetPriceStatus(priceCache_.IsRefreshInProgress(), priceCache_.GetPriceCount());

        ui_.Pump();
        overlay_.PumpMessages();

        HandleUIActions();

        UpdateOverlay();

        overlay_.SetFontSize(config_->overlayFontSize);

        UpdateRegionPreview();

        static auto lastTop = std::chrono::steady_clock::now();

        auto now = std::chrono::steady_clock::now();

        if (now - lastTop > std::chrono::seconds(2))
        {
            overlay_.BringToTop();
            lastTop = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void RuneHelperApp::HandleUIActions()
{
    if (!ui_.WantsSelectRegion())
        return;

    RegionSelector selector;

    cv::Rect newRegion = selector.Select();

    if (newRegion.empty())
        return;

    region_ = newRegion;

    config_->regionX = region_.x;
    config_->regionY = region_.y;
    config_->regionW = region_.width;
    config_->regionH = region_.height;

    configManager_.Save();
}

void RuneHelperApp::UpdateOverlay()
{
    if (!overlayDirty_.exchange(false))
        return;

    std::lock_guard lock(overlayMutex_);
    overlay_.SetTexts(sharedTexts_);
}

void RuneHelperApp::UpdateRegionPreview()
{
    if (!ui_.IsRegionHovered())
        return;

    RECT rect{
        config_->regionX,
        config_->regionY,
        config_->regionX + config_->regionW,
        config_->regionY + config_->regionH
    };

    overlay_.SetRegionPreview(config_->regionW > 0, rect);
}

void RuneHelperApp::Shutdown()
{
    running_ = false;
    updateChecker_.Stop();
}