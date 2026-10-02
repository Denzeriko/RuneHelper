#pragma once

#include <imgui.h>

#include "ui/ImGuiStyleSetup.h"

inline float UiScaled(float pixels)
{
    return pixels * ImGui::GetFontSize() / ImGuiStyleSetup::kBaseFontSize;
}
