#include "ui/ImGuiStyleSetup.h"

#include <filesystem>
#include <string>
#include <system_error>

#include <imgui.h>

#include "nlohmann/json.hpp"

#include "core/Logger.h"
#include "platform/PlatformPaths.h"
#include "ui/TextRaster.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

namespace
{
struct ScriptFont
{
    const char* path;
    int index;
};

#ifdef _WIN32
const ScriptFont kHangulFonts[] = { { "C:/Windows/Fonts/malgun.ttf", 0 }, { "C:/Windows/Fonts/gulim.ttc", 0 } };
const ScriptFont kJapaneseFonts[] = { { "C:/Windows/Fonts/YuGothR.ttc", 0 },
                                      { "C:/Windows/Fonts/meiryo.ttc", 0 },
                                      { "C:/Windows/Fonts/msgothic.ttc", 0 } };
const ScriptFont kThaiFonts[] = { { "C:/Windows/Fonts/LeelawUI.ttf", 0 }, { "C:/Windows/Fonts/tahoma.ttf", 0 } };
#else
const ScriptFont kHangulFonts[] = {
    { "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 1 },
    { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 1 },
    { "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 1 },
    { "/usr/share/fonts/truetype/nanum/NanumGothic.ttf", 0 },
};
const ScriptFont kJapaneseFonts[] = {
    { "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0 },
    { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0 },
    { "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 0 },
    { "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf", 0 },
};
const ScriptFont kThaiFonts[] = {
    { "/usr/share/fonts/noto/NotoSansThai-Regular.ttf", 0 },
    { "/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf", 0 },
    { "/usr/share/fonts/truetype/tlwg/Loma.ttf", 0 },
};
#endif

ImVector<ImWchar> gItemNameGlyphs;

void BuildItemNameGlyphs()
{
    ImFontGlyphRangesBuilder builder;
    const nlohmann::json recipes = nlohmann::json::parse(LoadEmbeddedRecipeDatabase(), nullptr, false);

    if (recipes.is_object() && recipes.contains("combinations") && recipes["combinations"].is_array())
    {
        for (const auto& entry : recipes["combinations"])
        {
            if (!entry.is_object() || !entry.contains("names") || !entry["names"].is_object())
                continue;

            for (const auto& name : entry["names"])
            {
                if (name.is_string())
                    builder.AddText(name.get<std::string>().c_str());
            }
        }
    }

    builder.BuildRanges(&gItemNameGlyphs);
}

template <std::size_t N>
void MergeFirstFound(const ScriptFont (&fonts)[N], const ImWchar* ranges)
{
    std::error_code ec;

    for (const ScriptFont& font : fonts)
    {
        if (!std::filesystem::exists(font.path, ec))
            continue;

        ImFontConfig config;
        config.MergeMode = true;
        config.FontNo = font.index;

        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(font.path, 13.0f, &config, ranges))
            return;
    }
}
}

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

    if (!io.Fonts->AddFontFromFileTTF(PathToUtf8(font).c_str(), 13.0f, &config, io.Fonts->GetGlyphRangesCyrillic()))
        LOG_ERROR("UI: could not add Cyrillic glyphs from " + PathToUtf8(font));

    BuildItemNameGlyphs();

    if (gItemNameGlyphs.empty())
        return;

    MergeFirstFound(kHangulFonts, gItemNameGlyphs.Data);
    MergeFirstFound(kJapaneseFonts, gItemNameGlyphs.Data);
    MergeFirstFound(kThaiFonts, gItemNameGlyphs.Data);
}
