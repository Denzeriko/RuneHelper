#pragma once

namespace ImGuiStyleSetup
{
#ifdef _WIN32
inline constexpr float kBaseFontSize = 16.0f;
#else
inline constexpr float kBaseFontSize = 13.0f;
#endif

void ApplyRuneHelperStyle();
void AddFonts();
}
