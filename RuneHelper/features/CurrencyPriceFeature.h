#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "core/Feature.h"
#include "platform/ClipboardWatcher.h"
#include "price/PriceService.h"

struct ItemPriceDiagnostic
{
    bool attempted = false;
    bool parsed = false;
    std::string clipboardHeader;
    std::string itemClass;
    std::string rarity;
    std::string name;
    std::string base;
    std::string league;
    std::string priceSource;
    std::string matchedName;
    std::string reason;
    int quantity = 1;
    int confidence = 0;
    std::optional<double> unitEx;
    std::size_t priceCount = 0;
    bool priceDataDownloading = false;
    bool priceRefreshFailed = false;
};

class CurrencyPriceFeature : public Feature
{
public:
    explicit CurrencyPriceFeature(PriceService& prices) : prices_(prices) {}

    std::string Name() const override { return "currency_price"; }

    bool Init(ConfigManager& configManager) override;
    void Shutdown() override;
    void Tick() override;
    void AppendOverlay(OverlayFrame& frame, const AppConfig& config) const override;
    void DrawDebug(UIManager& manager) override;

private:
    void ReadClipboard(std::string_view text, const AppConfig& config);

    PriceService& prices_;
    ConfigManager* configManager_ = nullptr;
    ClipboardWatcher clipboard_;
    bool watcherAttempted_ = false;
    std::string itemName_;
    std::string itemBase_;
    std::optional<ItemPriceDiagnostic> lastDiagnostic_;
    std::optional<double> unitEx_;
    double divineToEx_ = 0.0;
    int quantity_ = 1;
    int x_ = 32;
    int y_ = 32;
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    int screenX_ = 0;
    int screenY_ = 0;
    int cursorAnchorX_ = 0;
    int cursorAnchorY_ = 0;
    bool hasCursorAnchor_ = false;
    std::chrono::steady_clock::time_point visibleUntil_{};
};
