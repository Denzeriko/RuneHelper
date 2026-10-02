#include "ui/UiWidgets.h"

#include <algorithm>

#include "ui/UiScale.h"

void UiWidgets::DrawIcon(Icon icon)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto point = [origin](float x, float y) { return ImVec2(origin.x + UiScaled(x), origin.y + UiScaled(y)); };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_CheckMark);
    const float stroke = UiScaled(1.3f);

    if (icon == Icon::RuneShape)
    {
        draw->AddRect(point(4, 2), point(16, 18), color, UiScaled(2), 0, stroke);
        const ImVec2 textSize = ImGui::CalcTextSize("R");
        const ImVec2 center = point(10, 10);
        draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), color, "R");
    }
    else if (icon == Icon::Map)
    {
        const ImVec2 outline[] = { point(2, 5),   point(7, 2),   point(13, 5), point(18, 2),
                                   point(18, 16), point(13, 19), point(7, 16), point(2, 19) };
        draw->AddPolyline(outline, IM_ARRAYSIZE(outline), color, ImDrawFlags_Closed, stroke);
        draw->AddLine(point(7, 2), point(7, 16), color, stroke);
        draw->AddLine(point(13, 5), point(13, 19), color, stroke);
    }
    else
    {
        draw->AddCircle(point(7, 8), UiScaled(5), color, 20, stroke);
        draw->AddCircle(point(13, 13), UiScaled(5), color, 20, stroke);
    }

    ImGui::Dummy(ImVec2(UiScaled(20), ImGui::GetFrameHeight()));
}

bool UiWidgets::Toggle(const char* label, bool& value)
{
    ImGui::PushID(label);
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - UiScaled(65)));
    ImGui::TextDisabled("%s", value ? "On" : "Off");
    ImGui::SameLine(right - UiScaled(30));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool pressed = ImGui::Button("##switch", ImVec2(UiScaled(30), ImGui::GetFrameHeight()));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (pressed)
        value = !value;

    const ImVec2 start = ImGui::GetItemRectMin();
    const float y = start.y + (ImGui::GetFrameHeight() - UiScaled(16)) * 0.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        ImVec2(start.x, y),
        ImVec2(start.x + UiScaled(30), y + UiScaled(16)),
        ImGui::GetColorU32(value ? ImGuiCol_CheckMark : ImGuiCol_FrameBgHovered),
        UiScaled(8)
    );
    draw->AddCircleFilled(
        ImVec2(start.x + UiScaled(value ? 22 : 8), y + UiScaled(8)),
        UiScaled(5),
        ImGui::GetColorU32(value ? ImGuiCol_WindowBg : ImGuiCol_TextDisabled)
    );
    ImGui::PopID();
    return pressed;
}

bool UiWidgets::Tab(const char* label, bool selected, float width)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    const bool pressed = ImGui::Button(label, ImVec2(width, UiScaled(28)));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    if (selected)
    {
        const ImVec2 left = ImGui::GetItemRectMin();
        const ImVec2 right = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(left.x, right.y - UiScaled(1)),
            ImVec2(right.x, right.y - UiScaled(1)),
            ImGui::GetColorU32(ImGuiCol_CheckMark),
            UiScaled(2)
        );
    }

    return pressed;
}

void UiWidgets::Field(const char* label, float width)
{
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - UiScaled(width)));
    ImGui::SetNextItemWidth(UiScaled(width));
}

void UiWidgets::Section(const char* label)
{
    ImGui::TextDisabled("%s", label);
}

void UiWidgets::Divider()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void UiWidgets::Keycap(const char* label)
{
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 size(textSize.x + UiScaled(12), ImGui::GetFrameHeight());
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), UiScaled(3));
    draw->AddRect(origin, end, ImGui::GetColorU32(ImGuiCol_Border), UiScaled(3));
    draw->AddText(ImVec2(origin.x + UiScaled(6), origin.y + (size.y - textSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label);
    ImGui::Dummy(size);
}
