#include <windows.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

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


int main()
{
    Logger::Instance().Init();

    LOG_INFO("RuneHelper started");

    ConfigManager configManager;
    configManager.Load();

    AppConfig& config = configManager.Get();

    HWND console = GetConsoleWindow();

    if (console)
        ShowWindow(console, SW_HIDE);

    cv::Rect region;

    LOG_INFO("Selecting region");
    RegionSelector selector;
    region = selector.Select();

    /*if (config.regionW > 0 && config.regionH > 0)
    {
        region = cv::Rect(
            config.regionX,
            config.regionY,
            config.regionW,
            config.regionH
        );
    }
    else
    {
        RegionSelector selector;
        region = selector.Select();

        config.regionX = region.x;
        config.regionY = region.y;
        config.regionW = region.width;
        config.regionH = region.height;

        configManager.Save();
    }*/

    if (console)
        ShowWindow(console, SW_SHOW);

    if (region.empty())
    {
        LOG_ERROR("Region not selected");
        return 0;
    }

    LOG_INFO("Selected region: x=" + 
        std::to_string(region.x) +
        " y=" + std::to_string(region.y) +
        " w=" + std::to_string(region.width) +
        " h=" + std::to_string(region.height)
    );

    LOG_INFO("Initializing OCR");
    OCR ocr;
    ocr.SetConfig(&config);
    std::string tessdataPath = PrepareTessdata();

    if (!ocr.Init(tessdataPath))
    {
        LOG_ERROR("Tesseract init failed");
        return 1;
    }

    // ocr.SetDebug(true);

    PriceCache priceCache;
    OverlayWindow overlay;
    overlay.SetFontSize(config.overlayFontSize);

    if (!overlay.Create())
    {
        LOG_ERROR("Overlay create failed");
        return 1;
    }

    auto lastRefreshCheck = std::chrono::steady_clock::now();

    while (true)
    {
        overlay.PumpMessages();

        auto now = std::chrono::steady_clock::now();
        if (now - lastRefreshCheck > std::chrono::seconds(10))
        {
            LOG_INFO("Initializing price cache");
            priceCache.RefreshIfNeeded();
            lastRefreshCheck = now;
        }

        cv::Mat img = CaptureRegion(region);
        if (img.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto loot = ocr.RecognizeLoot(img);

        std::vector<OverlayText> overlayTexts;

        for (const auto& item : loot)
        {
            std::string rawName = ExtractItemName(item.text);
            std::optional<std::string> price = priceCache.GetPrice(rawName);

            if (!price)
            {
                auto guess = FindBestItemMatch(rawName, priceCache.GetAllItemNames());

                if (guess)
                {
                    price = priceCache.GetPrice(*guess);
                }
            }

            if (!price)
                continue;

            OverlayText t;
            t.text = ToWide(*price);
            t.x = region.x + region.width + config.overlayOffsetX;
            t.y = region.y + (item.y1 + item.y2) / 2 + config.overlayOffsetY;
            overlayTexts.push_back(std::move(t));
        }

        overlay.SetTexts(std::move(overlayTexts));

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(config.ocrIntervalMs));
    }

    return 0;
}