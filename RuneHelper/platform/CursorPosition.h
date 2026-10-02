#pragma once

#include <optional>

struct CursorPosition
{
    int x = 0;
    int y = 0;
    int screenWidth = 0;
    int screenHeight = 0;
    int screenX = 0;
    int screenY = 0;
};

std::optional<CursorPosition> QueryCursorPosition();
