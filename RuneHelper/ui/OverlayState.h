#pragma once

#include <cstdint>
#include <string>
#include <vector>

using OverlayColor = std::uint32_t;

struct OverlayRect
{
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

constexpr OverlayColor OverlayRgb(int r, int g, int b)
{
    return static_cast<OverlayColor>(
        (r & 0xff) |
        ((g & 0xff) << 8) |
        ((b & 0xff) << 16)
    );
}

struct OverlayText
{
    std::wstring text;
    int x = 0;
    int y = 0;
    OverlayColor color = OverlayRgb(255, 255, 255);
};

struct OverlayState
{
    bool running = false;
    bool visible = true;
    bool previewEnabled = false;
    bool clickThrough = true;
    bool alwaysOnTop = true;
    bool debugVisible = false;

    OverlayRect previewRect{};
    int fontSize = 24;

    std::vector<OverlayText> texts;
};
