#pragma once

#include <imgui.h>

namespace UiWidgets
{
enum class Icon
{
    RuneShape,
    Map,
    Currency
};

void DrawIcon(Icon icon);
bool Toggle(const char* label, bool& value);
bool Tab(const char* label, bool selected, float width = 0.0f);
void Field(const char* label, float width = 170.0f);
void Keycap(const char* label);
void Section(const char* label);
void Divider();
}
