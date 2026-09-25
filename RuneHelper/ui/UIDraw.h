#pragma once

class UIManager;

namespace UIDraw
{
inline constexpr int kWindowWidth = 420;
inline constexpr int kWindowHeight = 510;

void Draw(UIManager& ui);
void CellText(const char* text);
}
