#include "features/MapCheckFeature.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>

#include <imgui.h>

#include "core/ConfigManager.h"
#include "common/JsonRead.h"
#include "common/Text.h"
#include "platform/GameFocus.h"
#include "ui/UIDraw.h"
#include "ui/OverlayPlacement.h"
#include "ui/UiTooltip.h"

#ifdef _WIN32
#include "platform/windows/RegionSelect.h"
#else
#include "platform/linux/RegionSelect.h"
#endif

namespace
{
constexpr ImVec4 kBlocked{ 1.0f, 0.3f, 0.3f, 1.0f };
constexpr ImVec4 kMatch{ 0.35f, 0.9f, 0.55f, 1.0f };
constexpr const char* kComparisons[] = { ">", ">=", "<", "<=", "=" };
constexpr int kCursorMoveThreshold = 15;
constexpr int kPanelDurationSeconds = 7;

const MapNumericRule* FindRule(
    const std::vector<MapNumericRule>& rules,
    MapRuleTarget target,
    std::string_view pattern,
    MapRuleList list
)
{
    const auto it = std::find_if(
        rules.begin(),
        rules.end(),
        [target, pattern, list](const MapNumericRule& rule)
        { return rule.target == target && rule.pattern == pattern && rule.list == list; }
    );
    return it == rules.end() ? nullptr : &*it;
}

bool Matches(const MapNumericRule& rule, double actual)
{
    switch (rule.comparison)
    {
    case MapComparison::Greater: return actual > rule.value;
    case MapComparison::GreaterEqual: return actual >= rule.value;
    case MapComparison::Less: return actual < rule.value;
    case MapComparison::LessEqual: return actual <= rule.value;
    case MapComparison::Equal: return std::abs(actual - rule.value) < 0.0001;
    }

    return false;
}

bool MatchesItem(const MapItem& item, const MapNumericRule& rule)
{
    if (rule.target == MapRuleTarget::Property)
    {
        return std::any_of(
            item.properties.begin(),
            item.properties.end(),
            [&rule](const MapProperty& property)
            { return property.hasValue && property.pattern == rule.pattern && Matches(rule, property.value); }
        );
    }

    return std::any_of(
        item.modifiers.begin(),
        item.modifiers.end(),
        [&rule](const MapModifier& modifier)
        { return modifier.hasValue && modifier.pattern == rule.pattern && Matches(rule, modifier.value); }
    );
}

std::string RuleText(const MapNumericRule& rule)
{
    const auto index = static_cast<std::size_t>(rule.comparison);
    std::ostringstream value;
    value << std::setprecision(6) << rule.value;
    return rule.pattern + " " + kComparisons[std::min(index, std::size(kComparisons) - 1)] + " " + value.str();
}
}

bool MapCheckFeature::Init(ConfigManager& configManager)
{
    configManager_ = &configManager;
    const auto settings = configManager.FeatureSettings(Name());
    cursorPosition_ = JsonValue(settings, "cursorPosition", false);
    overlayArea_.x = std::clamp(JsonValue(settings, "overlayX", 0), -100000, 100000);
    overlayArea_.y = std::clamp(JsonValue(settings, "overlayY", 0), -100000, 100000);
    overlayArea_.width = std::clamp(JsonValue(settings, "overlayWidth", 0), 0, 2048);
    overlayArea_.height = std::clamp(JsonValue(settings, "overlayHeight", 0), 0, 1440);
    auto rules = settings.find("blacklistModifiers");

    if (rules == settings.end())
        rules = settings.find("warnings");

    if (rules != settings.end() && rules->is_array())
    {
        for (const auto& rule : *rules)
        {
            if (!rule.is_string())
                continue;

            const auto value = rule.get<std::string>();

            if (!value.empty() && value.size() <= 2048 && blacklistModifiers_.size() < 512 &&
                std::find(blacklistModifiers_.begin(), blacklistModifiers_.end(), value) == blacklistModifiers_.end())
                blacklistModifiers_.push_back(value);
        }
    }

    const auto numericRules = settings.find("numericRules");

    if (numericRules != settings.end() && numericRules->is_array())
    {
        for (const auto& entry : *numericRules)
        {
            if (!entry.is_object() || !entry.contains("pattern") || !entry["pattern"].is_string() || !entry.contains("value") ||
                !entry["value"].is_number() || !entry.contains("comparison") || !entry["comparison"].is_number_integer())
                continue;

            const auto pattern = entry["pattern"].get<std::string>();
            const auto target = JsonValue(entry, "target", "modifier");
            const auto list = JsonValue(entry, "list", "whitelist");
            const int comparison = JsonValue(entry, "comparison", 0);
            const double value = entry["value"].get<double>();

            if (pattern.empty() || pattern.size() > 2048 || numericRules_.size() >= 256 || !std::isfinite(value) ||
                std::abs(value) > 1000000.0 || comparison < 0 || static_cast<std::size_t>(comparison) >= std::size(kComparisons))
                continue;

            numericRules_.push_back({ pattern,
                                      target == "property" ? MapRuleTarget::Property : MapRuleTarget::Modifier,
                                      list == "blacklist" ? MapRuleList::Blacklist : MapRuleList::Whitelist,
                                      static_cast<MapComparison>(comparison),
                                      value });
        }
    }

    return true;
}

void MapCheckFeature::Shutdown()
{
    clipboard_.Stop();
}

void MapCheckFeature::Tick()
{
    if (!configManager_->Snapshot().showMapsTab)
    {
        clipboard_.Stop();
        watcherAttempted_ = false;
        preview_ = false;
        visibleUntil_ = {};
        hasCursorAnchor_ = false;
        return;
    }

    if (chooseArea_)
    {
        chooseArea_ = false;
        RegionSelector selector;
        const auto selected = selector.Select();

        if (!selected.empty())
        {
            if (selected.width < 240 || selected.height < 140)
                error_ = "Select an area at least 240 by 140 pixels.";
            else
            {
                overlayArea_ = selected;
                overlayArea_.width = std::min(overlayArea_.width, 2048);
                overlayArea_.height = std::min(overlayArea_.height, 1440);
                preview_ = true;
                visibleUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(kPanelDurationSeconds);
                error_.clear();
                StoreSettings();
            }
        }
    }

    if (!watcherAttempted_)
    {
        watcherAttempted_ = true;
        clipboard_.Start();
    }

    if (const auto text = clipboard_.Poll())
        ReadItem(*text);

    if (cursorPosition_ && hasCursorAnchor_ && item_)
    {
        if (const auto cursor = QueryCursorPosition())
        {
            const int dx = cursor->x - cursorAnchorX_;
            const int dy = cursor->y - cursorAnchorY_;

            if (dx * dx + dy * dy >= kCursorMoveThreshold * kCursorMoveThreshold)
            {
                visibleUntil_ = {};
                hasCursorAnchor_ = false;
            }
        }
    }
}

void MapCheckFeature::AppendOverlay(OverlayFrame& frame, const AppConfig& config) const
{
    const bool useCursor = cursorPosition_ && hasCursorAnchor_ && screenWidth_ > 0 && screenHeight_ > 0;

    if (!config.showMapsTab || (!useCursor && overlayArea_.empty()) || std::chrono::steady_clock::now() >= visibleUntil_)
        return;

    if (!preview_ && config.pauseWhenGameInactive && QueryGameFocus() == GameFocus::Inactive)
        return;

    OverlayPanel panel;
    panel.x = overlayArea_.x;
    panel.y = overlayArea_.y;
    panel.width = overlayArea_.width;
    panel.height = overlayArea_.height;
    panel.fontSize = config.mapsFontSize;
    panel.background = config.mapsPanelBackground;
    panel.outline = config.mapsPanelOutline;

    if (preview_)
    {
        panel.lines.push_back({ "Map check preview", OverlayRgb(255, 220, 110) });
        panel.lines.push_back({ "Copied Waystones and Tablets will appear here." });
        panel.lines.push_back({ "Whitelist matches are green; blacklist matches are red." });
    }
    else if (item_)
    {
        panel.lines.push_back({ item_->name, OverlayRgb(255, 220, 110) });

        if (!item_->base.empty())
            panel.lines.push_back({ item_->base, OverlayRgb(180, 180, 180) });

        const bool hasWhitelist = std::any_of(
            numericRules_.begin(),
            numericRules_.end(),
            [](const MapNumericRule& rule) { return rule.list == MapRuleList::Whitelist; }
        );
        const bool hasBlacklist =
            !blacklistModifiers_.empty() || std::any_of(
                                                numericRules_.begin(),
                                                numericRules_.end(),
                                                [](const MapNumericRule& rule) { return rule.list == MapRuleList::Blacklist; }
                                            );
        bool whitelistPassed = true;
        bool blacklistHit = false;
        std::vector<OverlayPanelLine> matched;

        for (const auto& rule : numericRules_)
        {
            const bool applies = MatchesItem(*item_, rule);

            if (rule.list == MapRuleList::Whitelist)
                whitelistPassed = whitelistPassed && applies;
            else
                blacklistHit = blacklistHit || applies;
        }

        for (const auto& property : item_->properties)
        {
            const auto matchingRule = [this, &property](MapRuleList list)
            {
                return std::any_of(
                    numericRules_.begin(),
                    numericRules_.end(),
                    [&property, list](const MapNumericRule& rule)
                    {
                        return rule.list == list && rule.target == MapRuleTarget::Property && rule.pattern == property.pattern &&
                               property.hasValue && Matches(rule, property.value);
                    }
                );
            };
            const bool allowed = matchingRule(MapRuleList::Whitelist);
            const bool blocked = matchingRule(MapRuleList::Blacklist);

            if (allowed || blocked)
                matched.push_back({ property.text, blocked ? OverlayRgb(255, 75, 75) : OverlayRgb(90, 225, 135) });
        }

        for (const auto& modifier : item_->modifiers)
        {
            const auto matchingRule = [this, &modifier](MapRuleList list)
            {
                return std::any_of(
                    numericRules_.begin(),
                    numericRules_.end(),
                    [&modifier, list](const MapNumericRule& rule)
                    {
                        return rule.list == list && rule.target == MapRuleTarget::Modifier && rule.pattern == modifier.pattern &&
                               modifier.hasValue && Matches(rule, modifier.value);
                    }
                );
            };
            const bool allowed = matchingRule(MapRuleList::Whitelist);
            const bool blocked = matchingRule(MapRuleList::Blacklist);
            const bool blockedByPattern =
                std::find(blacklistModifiers_.begin(), blacklistModifiers_.end(), modifier.pattern) != blacklistModifiers_.end();
            blacklistHit = blacklistHit || blockedByPattern;

            if (allowed || blocked || blockedByPattern)
                matched.push_back({ modifier.text, (blocked || blockedByPattern) ? OverlayRgb(255, 75, 75) : OverlayRgb(90, 225, 135) }
                );
        }

        if (hasWhitelist)
            panel.lines.push_back({ whitelistPassed ? "Whitelist: PASS" : "Whitelist: FAIL",
                                    whitelistPassed ? OverlayRgb(90, 225, 135) : OverlayRgb(255, 75, 75) });

        if (hasBlacklist)
            panel.lines.push_back({ blacklistHit ? "Blacklist: HIT" : "Blacklist: CLEAR",
                                    blacklistHit ? OverlayRgb(255, 75, 75) : OverlayRgb(90, 225, 135) });

        if (hasWhitelist || hasBlacklist)
        {
            const bool accepted = whitelistPassed && !blacklistHit;
            panel.lines.push_back({ accepted ? "Filter: ACCEPT" : "Filter: REJECT",
                                    accepted ? OverlayRgb(90, 225, 135) : OverlayRgb(255, 75, 75) });
        }

        if (hasWhitelist && !whitelistPassed)
        {
            for (const auto& rule : numericRules_)
            {
                if (rule.list == MapRuleList::Whitelist && !MatchesItem(*item_, rule))
                    matched.push_back({ "Required: " + RuleText(rule), OverlayRgb(255, 75, 75) });
            }
        }

        if (!matched.empty())
            panel.lines.insert(panel.lines.end(), matched.begin(), matched.end());
        else if (!hasWhitelist && !hasBlacklist)
            panel.lines.push_back({ "No filters configured. Add whitelist or blacklist rules in Maps." });
        else if (!hasWhitelist && !blacklistHit)
            panel.lines.push_back({ "No blacklist rules matched." });
    }
    else
        return;

    if (useCursor)
    {
        panel.width = std::min(720, std::max(1, screenWidth_ - 2 * kOverlayPanelMargin));
        const int desiredHeight = std::max(180, static_cast<int>(panel.lines.size()) * (panel.fontSize * 3 / 2 + 8) + 24);
        panel.height = std::min(desiredHeight, std::max(1, screenHeight_ - 2 * kOverlayPanelMargin));
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
    }

    frame.panels.push_back(std::move(panel));
}

void MapCheckFeature::StoreSettings()
{
    nlohmann::json numericRules = nlohmann::json::array();

    for (const auto& rule : numericRules_)
    {
        numericRules.push_back({ { "pattern", rule.pattern },
                                 { "target", rule.target == MapRuleTarget::Property ? "property" : "modifier" },
                                 { "list", rule.list == MapRuleList::Whitelist ? "whitelist" : "blacklist" },
                                 { "comparison", static_cast<int>(rule.comparison) },
                                 { "value", rule.value } });
    }

    configManager_->SetFeatureSettings(
        Name(),
        { { "blacklistModifiers", blacklistModifiers_ },
          { "warnings", blacklistModifiers_ },
          { "numericRules", numericRules },
          { "cursorPosition", cursorPosition_ },
          { "overlayX", overlayArea_.x },
          { "overlayY", overlayArea_.y },
          { "overlayWidth", overlayArea_.width },
          { "overlayHeight", overlayArea_.height } }
    );
}

void MapCheckFeature::ReadItem(std::string_view text)
{
    std::optional<MapItem> parsed = ParseMapItem(text);
    preview_ = false;
    visibleUntil_ = {};
    hasCursorAnchor_ = false;

    if (parsed)
    {
        item_ = std::move(parsed);
        visibleUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(kPanelDurationSeconds);
        if (cursorPosition_)
        {
            if (const auto cursor = QueryCursorPosition())
            {
                cursorAnchorX_ = cursor->x;
                cursorAnchorY_ = cursor->y;
                screenX_ = cursor->screenX;
                screenY_ = cursor->screenY;
                screenWidth_ = cursor->screenWidth;
                screenHeight_ = cursor->screenHeight;
                hasCursorAnchor_ = true;
            }
        }
    }
}

void MapCheckFeature::DrawItem()
{
    if (!item_)
        return;

    ImGui::Separator();
    UIDraw::CellText(item_->name.c_str());

    if (!item_->base.empty())
        UIDraw::CellText(item_->base.c_str());

    ImGui::TextDisabled("%s%s", item_->rarity.c_str(), item_->corrupted ? " / Corrupted" : "");

    const bool hasWhitelist = std::any_of(
        numericRules_.begin(),
        numericRules_.end(),
        [](const MapNumericRule& rule) { return rule.list == MapRuleList::Whitelist; }
    );
    const bool hasBlacklist =
        !blacklistModifiers_.empty() || std::any_of(
                                            numericRules_.begin(),
                                            numericRules_.end(),
                                            [](const MapNumericRule& rule) { return rule.list == MapRuleList::Blacklist; }
                                        );
    const bool whitelistPassed = std::all_of(
        numericRules_.begin(),
        numericRules_.end(),
        [this](const MapNumericRule& rule) { return rule.list != MapRuleList::Whitelist || MatchesItem(*item_, rule); }
    );
    const bool blacklistHit =
        std::any_of(
            numericRules_.begin(),
            numericRules_.end(),
            [this](const MapNumericRule& rule) { return rule.list == MapRuleList::Blacklist && MatchesItem(*item_, rule); }
        ) ||
        std::any_of(
            item_->modifiers.begin(),
            item_->modifiers.end(),
            [this](const MapModifier& modifier) {
                return std::find(blacklistModifiers_.begin(), blacklistModifiers_.end(), modifier.pattern) !=
                       blacklistModifiers_.end();
            }
        );

    if (hasWhitelist)
        ImGui::TextColored(whitelistPassed ? kMatch : kBlocked, "Whitelist: %s", whitelistPassed ? "PASS" : "FAIL");

    if (hasBlacklist)
        ImGui::TextColored(blacklistHit ? kBlocked : kMatch, "Blacklist: %s", blacklistHit ? "HIT" : "CLEAR");

    if (hasWhitelist || hasBlacklist)
    {
        const bool accepted = whitelistPassed && !blacklistHit;
        ImGui::TextColored(accepted ? kMatch : kBlocked, "Filter: %s", accepted ? "ACCEPT" : "REJECT");
    }

    if (!hasWhitelist && !hasBlacklist)
        ImGui::TextUnformatted("Add conditions to the whitelist or blacklist below.");

    ImGui::SeparatorText("ITEM PROPERTIES");

    for (const auto& property : item_->properties)
    {
        const auto* allowedRule = property.hasValue
                                      ? FindRule(numericRules_, MapRuleTarget::Property, property.pattern, MapRuleList::Whitelist)
                                      : nullptr;
        const auto* blockedRule = property.hasValue
                                      ? FindRule(numericRules_, MapRuleTarget::Property, property.pattern, MapRuleList::Blacklist)
                                      : nullptr;
        const bool allowed = allowedRule && Matches(*allowedRule, property.value);
        const bool blocked = blockedRule && Matches(*blockedRule, property.value);

        if (allowed || blocked)
            ImGui::PushStyleColor(ImGuiCol_Text, blocked ? kBlocked : kMatch);

        UIDraw::CellText(property.text.c_str());

        if (allowed || blocked)
            ImGui::PopStyleColor();

        if (property.hasValue)
        {
            ImGui::Indent();
            DrawNumericCondition(property.pattern, MapRuleTarget::Property, property.value);
            ImGui::Unindent();
        }
    }

    ImGui::SeparatorText("MODIFIERS");

    if (item_->modifiers.empty())
        ImGui::TextWrapped("No modifier lines found in the copied item.");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##modifierSearch", "Search modifiers...", modifierSearch_.data(), modifierSearch_.size());
    const std::string search = ToLowerAscii(modifierSearch_.data());

    for (std::size_t i = 0; i < item_->modifiers.size(); ++i)
    {
        const auto& modifier = item_->modifiers[i];
        const std::string normalized = ToLowerAscii(modifier.text);
        const std::string_view rawSearch(modifierSearch_.data());

        if (!search.empty() && normalized.find(search) == std::string::npos && modifier.text.find(rawSearch) == std::string::npos)
            continue;

        ImGui::PushID(static_cast<int>(i));

        const auto* allowedRule = FindRule(numericRules_, MapRuleTarget::Modifier, modifier.pattern, MapRuleList::Whitelist);
        const auto* blockedRule = FindRule(numericRules_, MapRuleTarget::Modifier, modifier.pattern, MapRuleList::Blacklist);
        const bool allowed = allowedRule && modifier.hasValue && Matches(*allowedRule, modifier.value);
        const bool blocked = blockedRule && modifier.hasValue && Matches(*blockedRule, modifier.value);
        const bool avoidAny =
            std::find(blacklistModifiers_.begin(), blacklistModifiers_.end(), modifier.pattern) != blacklistModifiers_.end();

        if (blocked || avoidAny)
            ImGui::PushStyleColor(ImGuiCol_Text, kBlocked);
        else if (allowed)
            ImGui::PushStyleColor(ImGuiCol_Text, kMatch);

        UIDraw::CellText(modifier.text.c_str());

        if (blocked || avoidAny || allowed)
            ImGui::PopStyleColor();

        if (!modifier.details.empty() && ImGui::IsItemHovered())
            UiTooltip(modifier.details.c_str());

        ImGui::Indent();

        if (modifier.hasValue)
            DrawNumericCondition(modifier.pattern, MapRuleTarget::Modifier, modifier.value);
        else if (ImGui::SmallButton(avoidAny ? "Avoid ✓" : "+ Avoid"))
        {
            if (avoidAny)
                std::erase(blacklistModifiers_, modifier.pattern);
            else
                blacklistModifiers_.push_back(modifier.pattern);

            StoreSettings();
        }

        ImGui::Unindent();
        ImGui::PopID();
    }
}

void MapCheckFeature::DrawNumericCondition(std::string_view pattern, MapRuleTarget target, double currentValue)
{
    ImGui::PushID(static_cast<int>(std::hash<std::string_view>{}(pattern)&0x7fffffffU));
    ImGui::PushID(static_cast<int>(target));

    const std::string key(pattern);

    for (const MapRuleList list : { MapRuleList::Whitelist, MapRuleList::Blacklist })
    {
        const auto* rule = FindRule(numericRules_, target, pattern, list);
        const char* label = list == MapRuleList::Whitelist ? "+ Keep" : "+ Avoid";
        const char* activeLabel = list == MapRuleList::Whitelist ? "Keep ✓" : "Avoid ✓";

        if (ImGui::SmallButton(rule ? activeLabel : label))
        {
            if (!rule)
            {
                numericRules_.push_back({ key, target, list, MapComparison::GreaterEqual, currentValue });
                StoreSettings();
            }

            editingPattern_ = key;
            editingTarget_ = target;
            editingList_ = list;
        }

        if (list == MapRuleList::Whitelist)
            ImGui::SameLine();
    }

    if (editingPattern_ == pattern && editingTarget_ == target)
    {
        auto it = std::find_if(
            numericRules_.begin(),
            numericRules_.end(),
            [this](const MapNumericRule& rule)
            { return rule.pattern == editingPattern_ && rule.target == editingTarget_ && rule.list == editingList_; }
        );

        if (it != numericRules_.end())
        {
            ImGui::Indent();
            ImGui::TextUnformatted(editingList_ == MapRuleList::Whitelist ? "Keep this item when:" : "Avoid this item when:");

            for (int i = 0; i < static_cast<int>(std::size(kComparisons)); ++i)
            {
                if (i > 0)
                    ImGui::SameLine();

                const bool selected = static_cast<int>(it->comparison) == i;

                if (ImGui::RadioButton(kComparisons[i], selected))
                {
                    it->comparison = static_cast<MapComparison>(i);
                    StoreSettings();
                }
            }

            double value = it->value;
            if (ImGui::InputDouble("Value", &value, 1.0, 10.0, "%.2f"))
            {
                it->value = std::clamp(value, -1000000.0, 1000000.0);
                StoreSettings();
            }

            if (target == MapRuleTarget::Modifier && editingList_ == MapRuleList::Blacklist)
            {
                bool avoidAny = std::find(blacklistModifiers_.begin(), blacklistModifiers_.end(), key) != blacklistModifiers_.end();

                if (ImGui::Checkbox("Avoid at any value", &avoidAny))
                {
                    if (avoidAny)
                        blacklistModifiers_.push_back(key);
                    else
                        std::erase(blacklistModifiers_, key);

                    StoreSettings();
                }
            }

            if (ImGui::SmallButton("Done"))
                editingPattern_.clear();

            ImGui::SameLine();

            if (ImGui::SmallButton("Remove condition"))
            {
                numericRules_.erase(it);
                StoreSettings();
                editingPattern_.clear();
            }

            ImGui::Unindent();
        }
        else
            editingPattern_.clear();
    }

    ImGui::PopID();
    ImGui::PopID();
}

void MapCheckFeature::DrawRules()
{
    ImGui::SeparatorText("ACTIVE FILTERS");
    ImGui::SeparatorText("KEEP");

    for (std::size_t i = 0; i < numericRules_.size();)
    {
        if (numericRules_[i].list != MapRuleList::Whitelist)
        {
            ++i;
            continue;
        }

        ImGui::PushID(static_cast<int>(i));
        const bool remove = ImGui::SmallButton("Remove");
        ImGui::SameLine();
        UIDraw::CellText(RuleText(numericRules_[i]).c_str());
        ImGui::PopID();

        if (remove)
        {
            if (editingPattern_ == numericRules_[i].pattern && editingList_ == MapRuleList::Whitelist)
                editingPattern_.clear();

            numericRules_.erase(numericRules_.begin() + static_cast<std::ptrdiff_t>(i));
            StoreSettings();
        }
        else
            ++i;
    }

    if (std::none_of(
            numericRules_.begin(),
            numericRules_.end(),
            [](const MapNumericRule& rule) { return rule.list == MapRuleList::Whitelist; }
        ))
        ImGui::TextDisabled("No keep conditions. Add one from an item property or modifier.");

    ImGui::SeparatorText("AVOID");

    for (std::size_t i = 0; i < blacklistModifiers_.size();)
    {
        ImGui::PushID(static_cast<int>(i + 2048));
        const bool remove = ImGui::SmallButton("Remove");
        ImGui::SameLine();
        UIDraw::CellText(blacklistModifiers_[i].c_str());
        ImGui::PopID();

        if (remove)
        {
            blacklistModifiers_.erase(blacklistModifiers_.begin() + static_cast<std::ptrdiff_t>(i));
            StoreSettings();
        }
        else
            ++i;
    }

    for (std::size_t i = 0; i < numericRules_.size();)
    {
        if (numericRules_[i].list != MapRuleList::Blacklist)
        {
            ++i;
            continue;
        }

        ImGui::PushID(static_cast<int>(i + 4096));
        const bool remove = ImGui::SmallButton("Remove");
        ImGui::SameLine();
        UIDraw::CellText(RuleText(numericRules_[i]).c_str());
        ImGui::PopID();

        if (remove)
        {
            if (editingPattern_ == numericRules_[i].pattern && editingList_ == MapRuleList::Blacklist)
                editingPattern_.clear();

            numericRules_.erase(numericRules_.begin() + static_cast<std::ptrdiff_t>(i));
            StoreSettings();
        }
        else
            ++i;
    }

    if (blacklistModifiers_.empty() && std::none_of(
                                           numericRules_.begin(),
                                           numericRules_.end(),
                                           [](const MapNumericRule& rule) { return rule.list == MapRuleList::Blacklist; }
                                       ))
        ImGui::TextDisabled("No avoid conditions.");
}

void MapCheckFeature::DrawTab(UIManager&)
{
    ImGui::BeginChild("MapCheckContent");
    DrawItem();
    DrawRules();
    ImGui::EndChild();
}

void MapCheckFeature::DrawSettings(UIManager&)
{
    const char* positionModes[] = { "Selected area", "Upper-right of cursor" };
    int positionMode = cursorPosition_ ? 1 : 0;

    if (ImGui::Combo("Panel position", &positionMode, positionModes, IM_ARRAYSIZE(positionModes)))
    {
        cursorPosition_ = positionMode == 1;
        hasCursorAnchor_ = false;

        if (cursorPosition_ && (item_ || preview_))
        {
            if (const auto cursor = QueryCursorPosition())
            {
                cursorAnchorX_ = cursor->x;
                cursorAnchorY_ = cursor->y;
                screenX_ = cursor->screenX;
                screenY_ = cursor->screenY;
                screenWidth_ = cursor->screenWidth;
                screenHeight_ = cursor->screenHeight;
                hasCursorAnchor_ = true;
            }
        }

        StoreSettings();
    }

    if (ImGui::Button("Choose overlay area"))
        chooseArea_ = true;

    if (!overlayArea_.empty() || cursorPosition_)
    {
        ImGui::SameLine();

        if (ImGui::Button("Preview"))
        {
            preview_ = true;
            visibleUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(kPanelDurationSeconds);

            if (cursorPosition_)
            {
                if (const auto cursor = QueryCursorPosition())
                {
                    cursorAnchorX_ = cursor->x;
                    cursorAnchorY_ = cursor->y;
                    screenX_ = cursor->screenX;
                    screenY_ = cursor->screenY;
                    screenWidth_ = cursor->screenWidth;
                    screenHeight_ = cursor->screenHeight;
                    hasCursorAnchor_ = true;
                }
            }
        }
    }
    else
        ImGui::TextDisabled("Choose an area on the game screen to show copied items.");

    if (watcherAttempted_ && !clipboard_.Running())
    {
        ImGui::TextWrapped("Background clipboard access is unavailable. Check desktop support and retry.");

        if (ImGui::Button("Retry clipboard access"))
            watcherAttempted_ = false;
    }
}
