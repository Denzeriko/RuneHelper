#include <windows.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <thread>
#include <atomic>
#include <mutex>

#include "Logger.h"

#include "Helpers.h"
#include "ConfigManager.h"
#include "NameNormalizer.h"
#include "OCR.h"
#include "Overlay.h"
#include "PriceCache.h"
#include "RegionSelect.h"
#include "ResourceHelper.h"
#include "ScreenCapture.h"
#include "UIManager.h"

#include "LootParser.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    Logger::Instance().Init();

    LOG_INFO("--------------------------");
    LOG_INFO("RuneHelper started! v1.1.1");

    ConfigManager configManager;
    configManager.Load();

    AppConfig& config = configManager.Get();

    cv::Rect region;

    if (config.regionW > 0 && config.regionH > 0)
    {
        region = cv::Rect(config.regionX, config.regionY, config.regionW, config.regionH);
    }
    else
    {
        RegionSelector selector;
        region = selector.Select();

        if (region.empty())
        {
            LOG_ERROR("Region not selected");
            return 0;
        }

        config.regionX = region.x;
        config.regionY = region.y;
        config.regionW = region.width;
        config.regionH = region.height;

        configManager.Save();
    }

    UIManager ui;
    if (!ui.Init(&config, &configManager))
    {
        LOG_ERROR("UI init failed");
        return 1;
    }

    OverlayWindow overlay;
    if (!overlay.Create())
    {
        LOG_ERROR("Overlay create failed");
        return 1;
    }
    overlay.SetFontSizeForce(config.overlayFontSize);

    PriceCache priceCache;

    OCR ocr;
    ocr.SetConfig(&config);

    std::mutex overlayMutex;
    std::vector<OverlayText> sharedTexts;

    // OCR initialization thread
    std::atomic<bool> running = true;
    std::atomic<bool> ocrReady = false;
    std::atomic<bool> ocrFailed = false;
    std::atomic<bool> ocrInitializing = true;

    std::jthread initThread([&]
        {
            ocrInitializing = true;

            LOG_INFO("Initializing OCR");

            std::string tessdataPath = PrepareTessdata();

            if (!ocr.Init(tessdataPath))
            {
                LOG_ERROR("Tesseract init failed");
                ocrFailed = true;
                ocrInitializing = false;
                return;
            }

            ocrReady = true;
            ocrInitializing = false;

            LOG_INFO("OCR ready");
        });

    // OCR worker
    std::atomic<bool> overlayDirty = false;

    std::jthread ocrThread([&]
        {
            while (running && !ocrReady)
            {
                if (ocrFailed)
                    return;

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto lastRefreshCheck = std::chrono::steady_clock::now();

            while (running)
            {
                auto now = std::chrono::steady_clock::now();

                if (now - lastRefreshCheck > std::chrono::seconds(10))
                {
                    priceCache.RefreshIfNeeded();
                    lastRefreshCheck = now;
                }

                if (!config.ocrEnabled)
                {
                    {
                        std::lock_guard lock(overlayMutex);
                        sharedTexts.clear();
                        overlayDirty = true;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                cv::Rect localRegion(config.regionX, config.regionY, config.regionW, config.regionH);

                cv::Mat img = CaptureRegion(localRegion);

                if (!img.empty())
                {
                    auto loot = ocr.RecognizeLoot(img);
                    std::vector<OverlayText> newTexts;

                    for (const auto& item : loot)
                    {
                        auto parsed = LootParser::ParseLootLine(item.text);
                        std::string rawName = parsed.itemName;
                        int quantity = parsed.quantity;
                        auto price = priceCache.GetPrice(rawName);

                        if (!price)
                        {
                            auto guess = FindBestItemMatch(rawName, priceCache.GetAllItemNames());

                            if (guess)
                                price = priceCache.GetPrice(*guess);
                        }

                        if (!price)
                            continue;

                        std::optional<double> value = LootParser::ParsePriceValue(*price);

                        OverlayText t;
                        double totalValue = value ? (*value * quantity) : 0.0;

                        t.color = GetPriceColor(totalValue, config);
                        t.text = ToWide(LootParser::FormatStackPrice(*price, quantity));
                        t.x = localRegion.x + localRegion.width + config.overlayOffsetX;
                        t.y = localRegion.y + (item.y1 + item.y2) / 2 + config.overlayOffsetY;

                        newTexts.push_back(std::move(t));
                    }

                    {
                        std::lock_guard lock(overlayMutex);
                        sharedTexts = std::move(newTexts);
                        overlayDirty = true;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(config.ocrIntervalMs));
            }
        });

    // Main loop
    while (ui.IsRunning())
    {
        ui.SetStatus(
            ocrInitializing.load(),
            ocrReady.load(),
            ocrFailed.load()
        );

        ui.Pump();
        overlay.PumpMessages();

        if (ui.WantsSelectRegion())
        {
            RegionSelector selector;
            cv::Rect newRegion = selector.Select();
            if (!newRegion.empty())
            {
                config.regionX = newRegion.x;
                config.regionY = newRegion.y;
                config.regionW = newRegion.width;
                config.regionH = newRegion.height;
                configManager.Save();
            }
        }

        if (overlayDirty.exchange(false))
        {
            std::lock_guard lock(overlayMutex);
            overlay.SetTexts(sharedTexts);
        }

        overlay.SetFontSize(config.overlayFontSize);

        static auto lastTopmost = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        if (now - lastTopmost > std::chrono::seconds(2))
        {
            overlay.BringToTop();
            lastTopmost = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    running = false;
    return 0;
}