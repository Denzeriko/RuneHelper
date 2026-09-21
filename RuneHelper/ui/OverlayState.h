#pragma once

#include <cstdint>
#include <cstdlib>
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
    std::string text;
    int x = 0;
    int y = 0;
    int fontSize = 0;
    OverlayColor color = OverlayRgb(255, 255, 255);
};

struct OverlayMark
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    OverlayColor color = OverlayRgb(255, 220, 80);
};

inline bool ApproxEqual(const OverlayText& a, const OverlayText& b, int yTolerance = 0)
{
    return a.x == b.x
        && std::abs(a.y - b.y) <= yTolerance
        && a.fontSize == b.fontSize
        && a.color == b.color
        && a.text == b.text;
}

inline bool ApproxEqual(const OverlayMark& a, const OverlayMark& b, int yTolerance = 0)
{
    return a.x == b.x
        && std::abs(a.y - b.y) <= yTolerance
        && a.width == b.width
        && a.height == b.height
        && a.color == b.color;
}

template <typename T>
bool ApproxEqual(const std::vector<T>& a, const std::vector<T>& b, int yTolerance = 0)
{
    if (a.size() != b.size())
        return false;

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (!ApproxEqual(a[i], b[i], yTolerance))
            return false;
    }

    return true;
}

struct OverlayFrame
{
    std::vector<OverlayText> texts;
    std::vector<OverlayMark> marks;

    bool Empty() const { return texts.empty() && marks.empty(); }

    bool ApproxEquals(const OverlayFrame& other, int yTolerance = 0) const
    {
        return ApproxEqual(texts, other.texts, yTolerance)
            && ApproxEqual(marks, other.marks, yTolerance);
    }
};

struct OverlayState
{
    bool running = false;
    bool visible = true;
    bool previewEnabled = false;
    bool clickThrough = true;
    bool alwaysOnTop = true;

    OverlayRect previewRect{};
    std::vector<OverlayMark> marks;
    int fontSize = 24;
    bool background = true;
    bool outline = false;

    std::vector<OverlayText> texts;
};
