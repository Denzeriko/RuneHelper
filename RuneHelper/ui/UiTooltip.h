#pragma once

#include <algorithm>

#include <imgui.h>

#include "ui/UiScale.h"

inline void UiTooltip(const char* text)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float windowFit = ImGui::GetMainViewport()->Size.x - 2.0f * (style.WindowPadding.x + style.DisplaySafeAreaPadding.x);
    const float wrap = std::max(UiScaled(80.0f), std::min(windowFit, UiScaled(360.0f)));

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
