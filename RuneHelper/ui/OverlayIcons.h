#pragma once

#include <string>
#include <string_view>

#include <opencv2/core.hpp>

struct OverlayIcon
{
    char32_t codePoint = 0;
    std::string_view file;
    std::string_view label;
};

inline constexpr OverlayIcon kExaltedOrbIcon{ U'\uE000', "exalted_orb.png", "ex" };
inline constexpr OverlayIcon kDivineOrbIcon{ U'\uE001', "divine_orb.png", "div" };
inline constexpr OverlayIcon kOverlayIcons[] = { kExaltedOrbIcon, kDivineOrbIcon };

const OverlayIcon* FindOverlayIcon(char32_t codePoint);
std::string IconString(const OverlayIcon& icon);
std::string WithIconLabels(std::string_view utf8);
cv::Mat LoadIconImage(const OverlayIcon& icon);
