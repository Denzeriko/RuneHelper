#pragma once

class UIManager;

namespace UIDraw
{
inline constexpr int kWindowWidth = 440;
inline constexpr int kWindowHeight = 620;

void Draw(UIManager& ui);
void CellText(const char* text);
}
