#include "features/CurrencyPriceFeature.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "core/ConfigManager.h"
#include "items/ItemText.h"
#include "ocr/LootParser.h"
#include "price/ResolvedPrice.h"
#include "platform/CursorPosition.h"
#include "platform/GameFocus.h"
#include "ui/OverlayIcons.h"
#include "ui/OverlayPlacement.h"
#include "ui/OverlayRenderer.h"
#include "ui/UIManager.h"

namespace
{
constexpr ImVec4 kDiagnosticSuccess{ 0.35f, 0.9f, 0.55f, 1.0f };
constexpr ImVec4 kDiagnosticWarning{ 1.0f, 0.8f, 0.2f, 1.0f };
constexpr ImVec4 kDiagnosticError{ 1.0f, 0.3f, 0.3f, 1.0f };
constexpr std::chrono::seconds kVisibleDuration{ 5 };
constexpr int kPanelWidth = 540;
constexpr int kCursorMoveThreshold = 15;

int PanelWidth(int fontSize)
{
    return std::max(kPanelWidth, fontSize * 18 + 160);
}

std::string AmountPair(double unitEx, double divineToEx, const std::string& exaltedUnit, const std::string& divineUnit)
{
    std::string text = LootParser::FormatAmount(unitEx, exaltedUnit);

    if (divineToEx > 0.0)
        text += "    " + LootParser::FormatAmount(unitEx / divineToEx, divineUnit);

    return text;
}

int StackQuantity(const ItemText& item)
{
    for (const auto& section : item.sections)
    {
        for (const std::string& line : section)
        {
            const std::size_t slash = line.find('/');

            if (slash == std::string::npos)
                continue;

            std::size_t begin = slash;

            while (begin > 0 && line[begin - 1] >= '0' && line[begin - 1] <= '9')
                --begin;

            if (begin == slash)
                continue;

            int value = 1;
            const auto parsed = std::from_chars(line.data() + begin, line.data() + slash, value);

            if (parsed.ec == std::errc{} && parsed.ptr == line.data() + slash && value > 0)
                return std::min(value, 1000000);
        }
    }

    return 1;
}

}

bool CurrencyPriceFeature::Init(ConfigManager& configManager)
{
    configManager_ = &configManager;
    return true;
}

void CurrencyPriceFeature::Shutdown()
{
    clipboard_.Stop();
}

void CurrencyPriceFeature::Tick()
{
    const AppConfig config = configManager_->Snapshot();

    if (!config.priceSearchEnabled || !config.currencyClipboardPriceEnabled)
    {
        clipboard_.Stop();
        watcherAttempted_ = false;
        itemName_.clear();
        itemBase_.clear();
        unitEx_.reset();
        divineToEx_ = 0.0;
        hasCursorAnchor_ = false;
        return;
    }

    if (!watcherAttempted_)
    {
        watcherAttempted_ = true;
        clipboard_.Start();
    }

    if (const auto text = clipboard_.Poll())
        ReadClipboard(*text, config);

    if (!hasCursorAnchor_ || itemName_.empty() || std::chrono::steady_clock::now() >= visibleUntil_)
        return;

    if (const auto cursor = QueryCursorPosition())
    {
        const int dx = cursor->x - cursorAnchorX_;
        const int dy = cursor->y - cursorAnchorY_;

        if (dx * dx + dy * dy >= kCursorMoveThreshold * kCursorMoveThreshold)
        {
            itemName_.clear();
            itemBase_.clear();
            unitEx_.reset();
            divineToEx_ = 0.0;
            visibleUntil_ = {};
            hasCursorAnchor_ = false;
        }
    }
}

void CurrencyPriceFeature::ReadClipboard(std::string_view text, const AppConfig& config)
{
    itemName_.clear();
    itemBase_.clear();
    unitEx_.reset();
    divineToEx_ = 0.0;
    visibleUntil_ = {};
    hasCursorAnchor_ = false;

    ItemPriceDiagnostic diagnostic;
    diagnostic.attempted = true;
    diagnostic.league = config.priceLeague;
    const PriceStatus status = prices_.Status();
    diagnostic.priceCount = status.priceCount;
    diagnostic.priceDataDownloading = status.downloading;
    diagnostic.priceRefreshFailed = status.refreshFailed;
    const std::size_t lineEnd = text.find_first_of("\r\n");
    diagnostic.clipboardHeader = std::string(text.substr(0, std::min<std::size_t>(lineEnd, 160)));

    const auto item = ParseItemText(text);

    if (!item)
    {
        diagnostic.reason = "Clipboard text was not recognised as supported item text.";
        lastDiagnostic_ = std::move(diagnostic);
        return;
    }

    diagnostic.parsed = true;
    diagnostic.itemClass = item->itemClass;
    diagnostic.rarity = item->rarity;
    diagnostic.name = item->name;
    diagnostic.base = item->base;
    diagnostic.quantity = StackQuantity(*item);

    ResolvedPrice price = prices_.Resolve(item->name, 1);
    bool trustedMatch = price.unitEx && price.confidence >= kTrustedMatchConfidence;
    bool matchedByBase = false;
    ResolvedPrice basePrice;

    if (!trustedMatch && !item->base.empty())
    {
        basePrice = prices_.Resolve(item->base, 1);

        if (basePrice.unitEx && basePrice.confidence >= kTrustedMatchConfidence)
        {
            price = basePrice;
            trustedMatch = true;
            matchedByBase = true;
        }
    }

    itemName_ = item->name;
    itemBase_ = item->base;
    quantity_ = diagnostic.quantity;
    divineToEx_ = prices_.DivineRate();

    diagnostic.confidence = price.confidence;
    diagnostic.matchedName = price.name;

    if (trustedMatch)
    {
        unitEx_ = price.unitEx;
        diagnostic.unitEx = price.unitEx;
        diagnostic.priceSource = matchedByBase ? "Base type" : price.name == item->name ? "Exact item name" : "Name match";
    }
    else
    {
        diagnostic.reason = "No trusted market match was found.";

        if (status.priceCount == 0)
        {
            if (status.downloading)
                diagnostic.reason = "Price data is still loading for this league.";
            else if (status.refreshFailed)
                diagnostic.reason = "Price data could not be loaded for this league.";
            else
                diagnostic.reason = "No market price data is loaded for this league.";
        }
        else if (price.confidence > 0 || basePrice.confidence > 0)
        {
            if (basePrice.confidence > price.confidence)
            {
                diagnostic.confidence = basePrice.confidence;
                diagnostic.matchedName = basePrice.name;
            }

            diagnostic.reason = "The best name match is below the trusted confidence threshold.";
        }
        else
            diagnostic.reason = "No exact or fuzzy match was found in the current price data.";

        if (status.refreshFailed && status.priceCount > 0)
            diagnostic.reason += " The latest price refresh failed; cached data is shown.";
    }

    lastDiagnostic_ = std::move(diagnostic);

    if (const auto cursor = QueryCursorPosition())
    {
        screenWidth_ = cursor->screenWidth;
        screenHeight_ = cursor->screenHeight;
        screenX_ = cursor->screenX;
        screenY_ = cursor->screenY;
        cursorAnchorX_ = cursor->x;
        cursorAnchorY_ = cursor->y;
        hasCursorAnchor_ = true;
    }
    else
    {
        if (config.regionW > 0 && config.regionH > 0)
        {
            x_ = config.regionX + config.regionW + kOverlayPanelGap;
            y_ = config.regionY;
        }
        else
        {
            x_ = 32;
            y_ = 32;
        }

        screenWidth_ = 0;
        screenHeight_ = 0;
        screenX_ = 0;
        screenY_ = 0;
    }

    visibleUntil_ = std::chrono::steady_clock::now() + kVisibleDuration;
}

void CurrencyPriceFeature::DrawDebug(UIManager& manager)
{
    const AppConfig& config = manager.ConfigDraft();

    if (!config.priceSearchEnabled)
    {
        ImGui::TextDisabled("Enable Price Search on the RuneHelper tab to load lookup data.");
        return;
    }

    if (!config.currencyClipboardPriceEnabled)
    {
        ImGui::TextDisabled("Enable Show copied item prices in Settings > Item Prices to inspect clipboard items.");
        return;
    }

    if (!lastDiagnostic_ || !lastDiagnostic_->attempted)
    {
        ImGui::TextDisabled("Copy an item with Ctrl+C to inspect parsing and price lookup details.");
        return;
    }

    const ItemPriceDiagnostic& diagnostic = *lastDiagnostic_;

    ImGui::Text("League: %s", diagnostic.league.c_str());
    ImGui::Text("Price entries loaded: %zu", diagnostic.priceCount);

    if (diagnostic.priceDataDownloading)
        ImGui::TextDisabled("Price data is downloading.");

    if (diagnostic.priceRefreshFailed)
        ImGui::TextColored(kDiagnosticWarning, "The latest price refresh failed.");

    if (!diagnostic.parsed)
    {
        ImGui::TextColored(kDiagnosticError, "%s", diagnostic.reason.c_str());
        ImGui::TextWrapped("The parser expects an item class, rarity, item name and the copied-item section separators.");

        if (!diagnostic.clipboardHeader.empty())
            ImGui::TextWrapped("Clipboard header: %s", diagnostic.clipboardHeader.c_str());

        return;
    }

    ImGui::Separator();
    ImGui::Text("Class: %s", diagnostic.itemClass.c_str());
    ImGui::Text("Rarity: %s", diagnostic.rarity.c_str());
    ImGui::Text("Name: %s", diagnostic.name.c_str());

    if (!diagnostic.base.empty())
        ImGui::Text("Base: %s", diagnostic.base.c_str());

    ImGui::Text("Stack size: %d", diagnostic.quantity);

    if (diagnostic.unitEx)
    {
        ImGui::TextColored(kDiagnosticSuccess, "Matched by: %s", diagnostic.priceSource.c_str());
        ImGui::Text("Market entry: %s", diagnostic.matchedName.c_str());
        ImGui::Text("Unit price: %.2f Exalted", *diagnostic.unitEx);
    }
    else
    {
        ImGui::TextColored(kDiagnosticWarning, "Price lookup: %s", diagnostic.reason.c_str());

        if (diagnostic.confidence > 0)
            ImGui::Text("Best candidate: %s (%d%%)", diagnostic.matchedName.c_str(), diagnostic.confidence);
    }
}

void CurrencyPriceFeature::AppendOverlay(OverlayFrame& frame, const AppConfig& config) const
{
    if (!config.priceSearchEnabled || !config.currencyClipboardPriceEnabled || itemName_.empty() ||
        std::chrono::steady_clock::now() >= visibleUntil_)
        return;

    if (config.pauseWhenGameInactive && QueryGameFocus() == GameFocus::Inactive)
        return;

    OverlayPanel panel;
    panel.x = x_;
    panel.y = y_;
    panel.width = PanelWidth(config.currencyPriceFontSize);
    panel.height = 1;
    panel.fontSize = config.currencyPriceFontSize;
    panel.background = config.pricePanelBackground;
    panel.outline = config.pricePanelOutline;

    if (hasCursorAnchor_ && screenWidth_ > 0)
        panel.width = std::min(panel.width, std::max(1, screenWidth_ - 2 * kOverlayPanelMargin));

    panel.lines.push_back({ itemName_, OverlayRgb(255, 220, 110) });

    if (!itemBase_.empty())
        panel.lines.push_back({ itemBase_, OverlayRgb(190, 190, 190) });

    if (unitEx_)
    {
        const std::string exaltedUnit = IconString(kExaltedOrbIcon);
        const std::string divineUnit = IconString(kDivineOrbIcon);
        panel.lines.push_back({ "Each: " + AmountPair(*unitEx_, divineToEx_, exaltedUnit, divineUnit), OverlayRgb(110, 220, 150) });

        if (quantity_ > 1)
        {
            panel.lines.push_back({ "Stack x" + std::to_string(quantity_) + ": " +
                                        AmountPair(*unitEx_ * quantity_, divineToEx_, exaltedUnit, divineUnit),
                                    OverlayRgb(110, 220, 150) });
        }
    }
    else
    {
        panel.lines.push_back({ "Price unavailable in the current poe.ninja data", OverlayRgb(190, 190, 190) });
    }

    panel.height = OverlayRenderer::PanelContentHeight(panel);

    if (hasCursorAnchor_ && screenWidth_ > 0 && screenHeight_ > 0)
    {
        panel.height = std::min(panel.height, std::max(1, screenHeight_ - 2 * kOverlayPanelMargin));
        const OverlayPosition position = PositionOverlayNearCursor(
            cursorAnchorX_,
            cursorAnchorY_,
            screenX_,
            screenY_,
            screenWidth_,
            screenHeight_,
            panel.width,
            panel.height
        );
        panel.x = position.x;
        panel.y = position.y;

        for (const OverlayPanel& existing : frame.panels)
        {
            if (!OverlayPanelsOverlap(panel, existing))
                continue;

            const int belowY = existing.y + existing.height + kOverlayPanelGap;
            const int aboveY = existing.y - panel.height - kOverlayPanelGap;
            const int bottom = screenY_ + screenHeight_ - kOverlayPanelMargin;
            const int top = screenY_ + kOverlayPanelMargin;

            if (belowY + panel.height <= bottom)
                panel.y = belowY;
            else if (aboveY >= top)
                panel.y = aboveY;
            else
            {
                const int rightX = existing.x + existing.width + kOverlayPanelGap;
                const int leftX = existing.x - panel.width - kOverlayPanelGap;
                const int left = screenX_ + kOverlayPanelMargin;
                const int right = screenX_ + screenWidth_ - kOverlayPanelMargin;

                if (rightX + panel.width <= right)
                    panel.x = rightX;
                else if (leftX >= left)
                    panel.x = leftX;
                else
                    panel.y = ClampOverlayPosition(belowY, top, screenHeight_ - 2 * kOverlayPanelMargin, panel.height);
            }
        }
    }

    frame.panels.push_back(std::move(panel));
}
