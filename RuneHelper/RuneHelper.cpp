#include <windows.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

#include "Helpers.h"
#include "NameNormalizer.h"
#include "OCR.h"
#include "Overlay.h"
#include "PriceCache.h"
#include "RegionSelect.h"
#include "ResourceHelper.h"
#include "ScreenCapture.h"

int main()
{
    RegionSelector selector;
    cv::Rect region = selector.Select();

    if (region.empty())
    {
        std::cout << "Region not selected\n";
        return 0;
    }

    OCR ocr;
    std::string tessdataPath = PrepareTessdata();

    if (!ocr.Init(tessdataPath))
    {
        std::cout << "Tesseract init failed\n";
        return 1;
    }

    // ocr.SetDebug(true);

    PriceCache priceCache;
    OverlayWindow overlay;

    if (!overlay.Create())
    {
        std::cout << "Overlay create failed\n";
        return 1;
    }

    auto lastRefreshCheck = std::chrono::steady_clock::now();

    while (true)
    {
        overlay.PumpMessages();

        auto now = std::chrono::steady_clock::now();
        if (now - lastRefreshCheck > std::chrono::seconds(10))
        {
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
            t.x = region.x + region.width + 20;
            t.y = region.y + (item.y1 + item.y2) / 2;
            overlayTexts.push_back(std::move(t));
        }

        overlay.SetTexts(std::move(overlayTexts));

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return 0;
}