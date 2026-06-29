#include "RuneHelperApp.h"

#include <chrono>

#include <opencv2/imgcodecs.hpp>

#include "core/Helpers.h"
#include "core/Logger.h"

#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"

#ifdef _WIN32
#include "platform/windows/RegionSelect.h"
#include "platform/windows/ResourceHelper.h"
#include "platform/windows/ScreenCapture.h"
#else
#include "platform/linux/RegionSelect.h"
#include "platform/linux/ResourceHelper.h"
#include "platform/linux/ScreenCapture.h"
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
    ocrReady_ = false;
    ocrFailed_ = false;

    LOG_INFO("Initializing OCR");

    if (!InitOcrEngine())
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

bool RuneHelperApp::InitOcrEngine()
{
    if (tessdataPath_.empty())
        tessdataPath_ = PrepareTessdata();

    std::lock_guard lock(ocrMutex_);
    ocr_.SetConfig(config_);
    return ocr_.Init(tessdataPath_);
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
            std::vector<OverlayText> newTexts;

#ifdef _WIN32
            std::vector<LootLine> loot;

            {
                std::lock_guard lock(ocrMutex_);
                loot = ocr_.RecognizeLoot(img);
            }

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
#else
            std::vector<OcrRowDebug> rows;

            {
                std::lock_guard lock(ocrMutex_);
                rows = ocr_.RecognizeRows(img, {config_->ocrPsm});
            }

            const int baseX = localRegion.x + localRegion.width + config_->overlayOffsetX;
            const int baseY = localRegion.y + config_->overlayOffsetY;
            const int lineStep = config_->overlayFontSize + 8;

            auto addDebugLine =
                [&](int lineIndex, const std::string& text, COLORREF color = RGB(220, 220, 220))
                {
                    OverlayText t;
                    t.color = color;
                    t.text = ToWide(text);
                    t.x = baseX;
                    t.y = baseY + lineIndex * lineStep;
                    newTexts.push_back(std::move(t));
                };

            if (rows.empty())
            {
                addDebugLine(0, "No items detected", RGB(180, 180, 180));
            }
            else
            {
                int lineIndex = 0;

                for (const auto& row : rows)
                {
                    auto parsed = LootParser::ParseLootLine(row.text);
                    std::string displayName = parsed.itemName;

                    auto price = priceCache_.GetPrice(displayName);

                    if (!price)
                    {
                        auto guess = FindBestItemMatch(displayName, priceCache_.GetAllItemNames());

                        if (guess)
                        {
                            displayName = *guess;
                            price = priceCache_.GetPrice(*guess);
                        }
                    }

                    if (price)
                    {
                        std::optional<double> value = LootParser::ParsePriceValue(*price);
                        double totalValue = value ? (*value * parsed.quantity) : 0.0;
                        COLORREF color = GetPriceColor(totalValue, *config_);
                        std::string colorName = "gray";

                        if (totalValue > config_->priceColorVeryHigh)
                            colorName = "red";
                        else if (totalValue > config_->priceColorHigh)
                            colorName = "yellow";
                        else if (totalValue > config_->priceColorMedium)
                            colorName = "green";

                        std::string line = displayName + " - " + LootParser::FormatStackPrice(*price, parsed.quantity) + " / " + colorName;

                        if (config_->debugOCR)
                            line += " (conf " + std::to_string(static_cast<int>(row.confidence)) + ")";

                        addDebugLine(lineIndex++, line, color);
                    }
                    else
                    {
                        std::string line = displayName + " - price unavailable";

                        if (config_->debugOCR)
                            line += " (conf " + std::to_string(static_cast<int>(row.confidence)) + ")";

                        addDebugLine(lineIndex++, line, RGB(160, 160, 160));
                    }
                }
            }
#endif

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
    if (ui_.WantsSelectRegion())
    {
        RegionSelector selector;

        cv::Rect newRegion = selector.Select();

        if (!newRegion.empty())
        {
            region_ = newRegion;

            config_->regionX = region_.x;
            config_->regionY = region_.y;
            config_->regionW = region_.width;
            config_->regionH = region_.height;

            configManager_.Save();
        }
    }

#ifndef _WIN32
    if (ui_.WantsResetOcr())
        ResetOcrEngine();

    if (ui_.WantsTestOcr())
        RunOcrDebugTest();
#endif
}

#ifndef _WIN32
void RuneHelperApp::ResetOcrEngine()
{
    LOG_INFO("Manual OCR engine reset requested");

    ocrInitializing_ = true;
    ocrReady_ = false;
    ocrFailed_ = false;

    if (!InitOcrEngine())
    {
        LOG_ERROR("Manual OCR engine reset failed");
        ocrFailed_ = true;
        ocrInitializing_ = false;
        return;
    }

    ocrReady_ = true;
    ocrInitializing_ = false;
    LOG_INFO("Manual OCR engine reset complete");
}

void RuneHelperApp::RunOcrDebugTest()
{
    if (!ocrReady_)
    {
        LOG_ERROR("Linux OCR debug test skipped: OCR is not ready");
        return;
    }

    cv::Rect localRegion(
        config_->regionX,
        config_->regionY,
        config_->regionW,
        config_->regionH
    );

    if (localRegion.empty())
    {
        LOG_ERROR("Linux OCR debug test skipped: no selected region");
        return;
    }

    LOG_INFO(
        "Linux OCR debug test capture region: x=" + std::to_string(localRegion.x) +
        " y=" + std::to_string(localRegion.y) +
        " w=" + std::to_string(localRegion.width) +
        " h=" + std::to_string(localRegion.height)
    );

    cv::Mat img = CaptureRegion(localRegion);

    if (img.empty())
    {
        LOG_ERROR("Linux OCR debug test failed: captured image is empty");
        return;
    }

    LOG_INFO(
        "Linux OCR debug capture dimensions: " +
        std::to_string(img.cols) + "x" + std::to_string(img.rows) +
        " channels=" + std::to_string(img.channels())
    );

    LOG_INFO("Linux OCR debug threshold: " + std::to_string(config_->ocrThreshold));

    if (cv::imwrite("debug_capture_full.png", img))
        LOG_INFO("Linux OCR debug saved full crop: debug_capture_full.png");
    else
        LOG_ERROR("Linux OCR debug failed to save full crop: debug_capture_full.png");

    std::vector<OcrRowDebug> rows;
    std::vector<cv::Rect> detectedRows;

    {
        std::lock_guard lock(ocrMutex_);
        cv::Mat prepared = ocr_.PreprocessForDebug(img);
        detectedRows = ocr_.DetectRows(prepared);
        cv::imwrite("debug_capture_threshold.png", prepared);
        rows = ocr_.RecognizeRows(img, {6, 7, 11}, "debug_capture");
    }

    LOG_INFO("Linux OCR debug detected rows before OCR: " + std::to_string(detectedRows.size()));

    for (size_t i = 0; i < detectedRows.size(); ++i)
    {
        const auto& row = detectedRows[i];
        LOG_INFO(
            "Linux OCR debug row " + std::to_string(i) +
            ": x=" + std::to_string(row.x) +
            " y=" + std::to_string(row.y) +
            " w=" + std::to_string(row.width) +
            " h=" + std::to_string(row.height)
        );
    }

    if (rows.empty())
    {
        LOG_INFO("Linux OCR debug row OCR text: <none>");
        LOG_INFO("Linux OCR debug parsed rows: <none>");
        return;
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];

        LOG_INFO(
            "Linux OCR debug row " + std::to_string(i) +
            " psm=" + std::to_string(row.psm) +
            " conf=" + std::to_string(row.confidence) +
            " raw=\"" + row.rawText + "\""
        );

        auto parsed = LootParser::ParseLootLine(row.text);

        if (parsed.itemName.empty())
        {
            LOG_INFO("Linux OCR debug parsed row " + std::to_string(i) + ": <none>");
            continue;
        }

        LOG_INFO(
            "Linux OCR debug parsed row " + std::to_string(i) +
            ": quantity=" + std::to_string(parsed.quantity) +
            " item=\"" + parsed.itemName + "\""
        );
    }
}
#endif

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
