#pragma once

class UIManager;

namespace UIDraw
{
inline constexpr int kWindowWidth = 440;
inline constexpr int kWindowHeight = 600;

void Draw(UIManager& ui);
void CellText(const char* text);
}
