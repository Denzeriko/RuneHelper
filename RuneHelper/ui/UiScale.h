#pragma once

#include <imgui.h>

inline float UiScaled(float pixels)
{
    constexpr float kBaseFontSize = 13.0f;

    return pixels * ImGui::GetFontSize() / kBaseFontSize;
}
