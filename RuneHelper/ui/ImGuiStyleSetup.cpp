#include "ui/ImGuiStyleSetup.h"

#include <filesystem>
#include <string>

#include <imgui.h>

#include "common/Logger.h"
#include "platform/PlatformPaths.h"
#include "ui/TextRaster.h"

void ImGuiStyleSetup::ApplyRuneHelperStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
}

void ImGuiStyleSetup::AddFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();

    const std::filesystem::path font = FindSystemFont();

    if (font.empty())
    {
        LOG_INFO("UI: no system font found, Cyrillic text in the window shows as '?'");
        return;
    }

    ImFontConfig config;
    config.MergeMode = true;

    static constexpr ImWchar kBaseRanges[] = { 0x20, 0x024F, 0x0400, 0x052F, 0x2000, 0x206F, 0x20A0, 0x20CF, 0 };

    if (!io.Fonts->AddFontFromFileTTF(PathToUtf8(font).c_str(), 13.0f, &config, kBaseRanges))
        LOG_ERROR("UI: could not add Cyrillic glyphs from " + PathToUtf8(font));

    for (const auto& fallback : FindScriptFonts())
    {
        const ImWchar* ranges = nullptr;

        switch (fallback.script)
        {
        case FontScript::Korean: ranges = io.Fonts->GetGlyphRangesKorean(); break;
        case FontScript::Japanese: ranges = io.Fonts->GetGlyphRangesChineseFull(); break;
        case FontScript::Thai: ranges = io.Fonts->GetGlyphRangesThai(); break;
        }

        config.FontNo = fallback.index;
        config.OversampleH = 1;
        config.OversampleV = 1;

        if (!io.Fonts->AddFontFromFileTTF(PathToUtf8(fallback.path).c_str(), 13.0f, &config, ranges))
            LOG_ERROR("UI: could not add glyphs from " + PathToUtf8(fallback.path));
    }
}
