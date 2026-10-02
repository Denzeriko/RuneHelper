#pragma once

#include <array>
#include <optional>
#include <chrono>
#include <string>
#include <vector>

#include "core/Feature.h"
#include "items/MapItem.h"
#include "platform/ClipboardWatcher.h"
#include "platform/CursorPosition.h"

enum class MapRuleTarget
{
    Property,
    Modifier
};

enum class MapRuleList
{
    Whitelist,
    Blacklist
};

enum class MapComparison
{
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Equal
};

struct MapNumericRule
{
    std::string pattern;
    MapRuleTarget target = MapRuleTarget::Modifier;
    MapRuleList list = MapRuleList::Whitelist;
    MapComparison comparison = MapComparison::Greater;
    double value = 0.0;
};

class MapCheckFeature : public Feature
{
public:
    std::string Name() const override { return "map_check"; }

    bool Init(ConfigManager& configManager) override;
    void Shutdown() override;
    void Tick() override;
    void AppendOverlay(OverlayFrame& frame, const AppConfig& config) const override;

    const char* TabTitle() const override { return "Maps"; }

    void DrawTab(UIManager& manager) override;
    void DrawSettings(UIManager& manager) override;

private:
    void ReadItem(std::string_view text);
    void DrawItem();
    void DrawRules();
    void DrawNumericCondition(std::string_view pattern, MapRuleTarget target, double currentValue);
    void StoreSettings();

    ConfigManager* configManager_ = nullptr;
    ClipboardWatcher clipboard_;
    bool watcherAttempted_ = false;
    bool chooseArea_ = false;
    bool preview_ = false;
    bool cursorPosition_ = false;
    cv::Rect overlayArea_;
    std::chrono::steady_clock::time_point visibleUntil_{};
    int cursorAnchorX_ = 0;
    int cursorAnchorY_ = 0;
    int screenX_ = 0;
    int screenY_ = 0;
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    bool hasCursorAnchor_ = false;
    std::optional<MapItem> item_;
    std::string error_;
    std::vector<std::string> blacklistModifiers_;
    std::vector<MapNumericRule> numericRules_;
    std::string editingPattern_;
    MapRuleTarget editingTarget_ = MapRuleTarget::Modifier;
    MapRuleList editingList_ = MapRuleList::Whitelist;
    std::array<char, 128> modifierSearch_{};
};
