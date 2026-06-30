#pragma once

class UIManager;
struct UIState;

namespace UIDraw
{
void Draw(UIManager& manager);
void DrawTitleBar(UIManager& manager, UIState& state);
void DrawMainTab(UIManager& manager, UIState& state);
void DrawDebugTab(UIManager& manager, UIState& state);
}
