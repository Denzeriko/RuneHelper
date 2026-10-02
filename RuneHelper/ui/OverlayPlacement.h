#pragma once

#include <algorithm>

#include "ui/OverlayState.h"

inline constexpr int kOverlayPanelGap = 24;
inline constexpr int kOverlayPanelMargin = 8;

struct OverlayPosition
{
    int x = 0;
    int y = 0;
};

inline int ClampOverlayPosition(int position, int origin, int extent, int panelExtent)
{
    return std::clamp(position, origin, std::max(origin, origin + extent - panelExtent));
}

inline OverlayPosition PositionOverlayNearCursor(
    int cursorX,
    int cursorY,
    int screenX,
    int screenY,
    int screenWidth,
    int screenHeight,
    int panelWidth,
    int panelHeight
)
{
    int x = cursorX + kOverlayPanelGap;
    int y = cursorY - panelHeight - kOverlayPanelGap;

    if (y < screenY + kOverlayPanelMargin)
        y = cursorY + kOverlayPanelGap;

    x = ClampOverlayPosition(x, screenX + kOverlayPanelMargin, screenWidth - 2 * kOverlayPanelMargin, panelWidth);
    y = ClampOverlayPosition(y, screenY + kOverlayPanelMargin, screenHeight - 2 * kOverlayPanelMargin, panelHeight);

    return { x, y };
}

inline bool OverlayPanelsOverlap(const OverlayPanel& left, const OverlayPanel& right)
{
    return left.x < right.x + right.width && left.x + left.width > right.x && left.y < right.y + right.height &&
           left.y + left.height > right.y;
}
