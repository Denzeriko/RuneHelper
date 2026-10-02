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

    const auto rgb = [](unsigned value)
    {
        return ImVec4(
            static_cast<float>((value >> 16) & 255) / 255.0f,
            static_cast<float>((value >> 8) & 255) / 255.0f,
            static_cast<float>(value & 255) / 255.0f,
            1.0f
        );
    };

    colors[ImGuiCol_Text] = rgb(0xececee);
    colors[ImGuiCol_TextDisabled] = rgb(0xa0a4ac);
    colors[ImGuiCol_WindowBg] = rgb(0x1b1d20);
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = rgb(0x23262a);
    colors[ImGuiCol_Border] = rgb(0x34373d);
    colors[ImGuiCol_Button] = rgb(0x292c31);
    colors[ImGuiCol_ButtonHovered] = rgb(0x34383f);
    colors[ImGuiCol_ButtonActive] = rgb(0x454951);
    colors[ImGuiCol_FrameBg] = rgb(0x292c31);
    colors[ImGuiCol_FrameBgHovered] = rgb(0x34383f);
    colors[ImGuiCol_FrameBgActive] = rgb(0x454951);
    colors[ImGuiCol_TitleBg] = rgb(0x202226);
    colors[ImGuiCol_TitleBgActive] = rgb(0x292c31);
    colors[ImGuiCol_Header] = rgb(0x39352d);
    colors[ImGuiCol_HeaderHovered] = rgb(0x454034);
    colors[ImGuiCol_HeaderActive] = rgb(0x554b38);
    colors[ImGuiCol_SliderGrab] = rgb(0xcbb58a);
    colors[ImGuiCol_SliderGrabActive] = rgb(0xe0c898);
    colors[ImGuiCol_CheckMark] = rgb(0xcbb58a);
    colors[ImGuiCol_Tab] = rgb(0x23262a);
    colors[ImGuiCol_TabHovered] = rgb(0x454034);
    colors[ImGuiCol_TabActive] = rgb(0x39352d);
    colors[ImGuiCol_Separator] = rgb(0x34373d);
    colors[ImGuiCol_SeparatorHovered] = rgb(0x8b7d61);
    colors[ImGuiCol_SeparatorActive] = rgb(0xcbb58a);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = rgb(0x454951);
    colors[ImGuiCol_ScrollbarGrabHovered] = rgb(0x5a5f68);
    colors[ImGuiCol_ScrollbarGrabActive] = rgb(0x747a85);
    colors[ImGuiCol_NavHighlight] = rgb(0xcbb58a);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.55f, 0.49f, 0.38f, 0.45f);

    style.WindowPadding = ImVec2(12, 8);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.CellPadding = ImVec2(4, 6);
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.ScrollbarSize = 8.0f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 3.0f;
    style.FrameBorderSize = 1.0f;
}

void ImGuiStyleSetup::AddFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    const std::filesystem::path font = FindSystemFont();
    static constexpr ImWchar kBaseRanges[] = { 0x20, 0x024F, 0x0400, 0x052F, 0x2000, 0x206F, 0x20A0, 0x20CF, 0x2713, 0x2713, 0 };

    if (font.empty() || !io.Fonts->AddFontFromFileTTF(PathToUtf8(font).c_str(), 13.0f, nullptr, kBaseRanges))
    {
        io.Fonts->AddFontDefault();
        LOG_INFO("UI: using the default font; some scripts may be unavailable");
    }

    ImFontConfig config;
    config.MergeMode = true;

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
