#include "RuneHelperApp.h"

#include <chrono>
#include <unordered_set>

#include "core/Helpers.h"
#include "core/Logger.h"
#include "core/Version.h"

#include "ocr/LootParser.h"
#include "ocr/NameNormalizer.h"

#include "platform/windows/RegionSelect.h"
#include "platform/windows/ResourceHelper.h"

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

    ui_.RegisterHotkeys();

    priceCache_.RefreshIfNeeded();

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

static bool HasCloseOverlayText(const std::vector<OverlayText>& texts, int y, int minDistance)
{
    for (const auto& t : texts)
    {
        if (std::abs(t.y - y) < minDistance)
            return true;
    }

    return false;
}

void RuneHelperApp::OcrWorkerLoop()
{
    while (running_ && !ocrReady_)
    {
        if (ocrFailed_)
            return;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    {
        std::lock_guard lock(cachedNamesMutex_);
        cachedItemNames_ = BuildCachedItemNames(priceCache_.GetAllItemNames());
    }

    auto lastRefreshCheck = std::chrono::steady_clock::now();

    while (running_)
    {
        if (std::chrono::steady_clock::now() - lastRefreshCheck > std::chrono::seconds(10))
        {
            lastRefreshCheck = std::chrono::steady_clock::now();
            priceCache_.RefreshIfNeeded();
            {
                std::lock_guard lock(cachedNamesMutex_);
                cachedItemNames_ = BuildCachedItemNames(priceCache_.GetAllItemNames());
            }
        }

        bool runSingleSnapshot = singleSnapshotRequested_.exchange(false);

        if (runSingleSnapshot)
            singleSnapshotUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        bool keepSnapshot = std::chrono::steady_clock::now() < singleSnapshotUntil_;

        if (!config_->ocrEnabled && !runSingleSnapshot && !keepSnapshot)
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

        cv::Mat img = screenCapture_.CaptureRegion(localRegion);

        //if (img.empty())
        //    img = CaptureRegion(localRegion); //old capture

        if (!img.empty())
        {
            auto loot = ocr_.RecognizeLoot(img);

            DebugData debug;
            std::vector<OverlayText> newTexts;

            std::vector<CachedItemName> cachedNames;
            {
                std::lock_guard lock(cachedNamesMutex_);
                cachedNames = cachedItemNames_;
            }

            /*if ((loot.size() < 2) && config_->ocrAutoDetect) //Broken! Cos OCR Passes making garbage lines and dupes
            {
                {
                    std::lock_guard lock(overlayMutex_);
                    sharedTexts_.clear();
                    overlayDirty_ = true;
                }

                {
                    std::lock_guard lock(debugMutex_);
                    debugData_ = std::move(debug);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(config_->ocrIntervalMs));
                continue;
            }*/

            for (const auto& item : loot)
            {
                DebugLine debugLine;
                debugLine.ocrText = item.text;
                debugLine.matchedText = "-";
                debugLine.price = "-";
                debugLine.confidence = 0;

                auto parsed = LootParser::ParseLootLine(item.text);

                std::string rawName = parsed.itemName;
                int quantity = parsed.quantity;

                auto price = priceCache_.GetPrice(rawName);

                if (price)
                {
                    debugLine.matchedText = rawName;
                    debugLine.confidence = 100;
                }
                else
                {
                    auto guess = FindBestItemMatch(rawName, cachedNames);

                    if (guess)
                    {
                        debugLine.matchedText = guess->name;
                        debugLine.confidence = guess->confidence;
                        price = priceCache_.GetPrice(guess->name);
                    }
                }

                if (!price)
                {
                    debug.lines.push_back(std::move(debugLine));
                    continue;
                }

                debugLine.price = *price;

                std::optional<double> value = LootParser::ParsePriceValue(*price);
                double totalValue = value ? (*value * quantity) : 0.0;

                int overlayY = localRegion.y + (item.y1 + item.y2) / 2 + config_->overlayOffsetY;

                if (HasCloseOverlayText(newTexts, overlayY, 25))
                {
                    debug.lines.push_back(std::move(debugLine));
                    continue;
                }

                OverlayText t;
                t.color = GetPriceColor(totalValue, *config_);
                t.text = ToWide(LootParser::FormatStackPrice(*price, quantity));
                t.x = localRegion.x + localRegion.width + config_->overlayOffsetX;
                t.y = localRegion.y + (item.y1 + item.y2) / 2 + config_->overlayOffsetY;

                newTexts.push_back(std::move(t));

                debug.lines.push_back(std::move(debugLine));
            }

            {
                std::lock_guard lock(overlayMutex_);
                sharedTexts_ = std::move(newTexts);
                overlayDirty_ = true;
            }

            {
                std::lock_guard lock(debugMutex_);
                debugData_ = std::move(debug);
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

        {
            std::lock_guard lock(debugMutex_);
            ui_.SetDebugData(debugData_);
        }

        ui_.Pump();
        overlay_.PumpMessages();

        HandleUIActions();

        UpdateRegionPreview();

        UpdateOverlay();

        overlay_.SetFontSize(config_->overlayFontSize);

        if (ui_.WantsToggleOCR())
        {
            config_->ocrEnabled =!config_->ocrEnabled;
            configManager_.Save();
        }

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
    if (ui_.WantsToggleOCR())
    {
        config_->ocrEnabled = !config_->ocrEnabled;
        configManager_.Save();
    }

    if (ui_.WantsSingleSnapshot())
    {
        singleSnapshotRequested_ = true;
    }

    if (ui_.WantsRefreshPrices())
    {
        //priceCache_.ForceRefreshAsync();
    }

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
    if (!ui_.IsRegionHovered() || config_->regionW <= 0)
    {
        static RECT empty{};
        overlay_.SetRegionPreview(false, empty);
        return;
    }

    RECT rect{
        config_->regionX,
        config_->regionY,
        config_->regionX + config_->regionW,
        config_->regionY + config_->regionH
    };

    overlay_.SetRegionPreview(true, rect);
}

void RuneHelperApp::Shutdown()
{
    running_ = false;
    ui_.RegisterHotkeys();
    screenCapture_.Shutdown();
    updateChecker_.Stop();
}