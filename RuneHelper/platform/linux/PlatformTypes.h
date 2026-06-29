#pragma once

#include <cstdint>

using COLORREF = std::uint32_t;

struct RECT
{
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

constexpr COLORREF RGB(int r, int g, int b)
{
    return static_cast<COLORREF>(
        (r & 0xff) |
        ((g & 0xff) << 8) |
        ((b & 0xff) << 16)
    );
}
