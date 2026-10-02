#include "ui/TextRaster.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "common/Logger.h"
#include "common/Text.h"
#include "ui/OverlayIcons.h"

namespace
{
constexpr int kMinPixelHeight = 6;
constexpr int kMaxPixelHeight = 256;
constexpr int kOutlineRadius = 2;
constexpr double kIconHeightPerCap = 1.4;

#ifdef _WIN32
const char* const kFontNames[] = {
    "segoeui.ttf",
    "arial.ttf",
    "tahoma.ttf",
    "verdana.ttf",
};

std::filesystem::path SystemFontDir()
{
    if (const char* windir = std::getenv("WINDIR"); windir && *windir)
        return std::filesystem::path(windir) / "Fonts";

    return "C:/Windows/Fonts";
}
#else
const char* const kFontCandidates[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/TTF/Roboto-Regular.ttf",
};
#endif

struct FontCandidate
{
    const char* path;
    int index;
};

#ifdef _WIN32
const FontCandidate kHangulFonts[] = { { "C:/Windows/Fonts/malgun.ttf", 0 }, { "C:/Windows/Fonts/gulim.ttc", 0 } };
const FontCandidate kJapaneseFonts[] = { { "C:/Windows/Fonts/YuGothR.ttc", 0 },
                                         { "C:/Windows/Fonts/meiryo.ttc", 0 },
                                         { "C:/Windows/Fonts/msgothic.ttc", 0 } };
const FontCandidate kThaiFonts[] = { { "C:/Windows/Fonts/LeelawUI.ttf", 0 }, { "C:/Windows/Fonts/tahoma.ttf", 0 } };
#else
const FontCandidate kHangulFonts[] = {
    { "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 1 },
    { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 1 },
    { "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 1 },
    { "/usr/share/fonts/truetype/nanum/NanumGothic.ttf", 0 },
};
const FontCandidate kJapaneseFonts[] = {
    { "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0 },
    { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0 },
    { "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 0 },
    { "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf", 0 },
};
const FontCandidate kThaiFonts[] = {
    { "/usr/share/fonts/noto/NotoSansThai-Regular.ttf", 0 },
    { "/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf", 0 },
    { "/usr/share/fonts/truetype/tlwg/Loma.ttf", 0 },
};
#endif

template <std::size_t N>
void FindScriptFont(std::vector<ScriptFont>& result, const FontCandidate (&candidates)[N], FontScript script)
{
    std::error_code ec;

    for (const auto& font : candidates)
    {
        if (std::filesystem::is_regular_file(font.path, ec))
        {
            result.push_back({ font.path, font.index, script });
            return;
        }
    }
}

std::vector<unsigned char> ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);

    if (!in)
        return {};

    const std::streamoff size = in.tellg();

    if (size <= 0)
        return {};

    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), size);

    if (!in)
        return {};

    return data;
}

std::filesystem::path ScanForFont(const std::filesystem::path& root)
{
    std::error_code ec;

    if (!std::filesystem::is_directory(root, ec))
        return {};

    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end;
         it.increment(ec))
    {
        if (ec)
            break;

        if (!it->is_regular_file(ec))
            continue;

        const std::filesystem::path& path = it->path();

        if (path.extension() == ".ttf" && path.filename().string().find("Mono") == std::string::npos)
            return path;
    }

    return {};
}

}

std::vector<ScriptFont> FindScriptFonts()
{
    std::vector<ScriptFont> result;
    FindScriptFont(result, kJapaneseFonts, FontScript::Japanese);
    FindScriptFont(result, kHangulFonts, FontScript::Korean);
    FindScriptFont(result, kThaiFonts, FontScript::Thai);
    return result;
}

std::filesystem::path FindSystemFont()
{
    if (const char* fromEnv = std::getenv("RUNEHELPER_OVERLAY_FONT"); fromEnv && *fromEnv)
    {
        std::error_code ec;

        if (std::filesystem::exists(fromEnv, ec))
            return fromEnv;

        LOG_ERROR("Overlay font from RUNEHELPER_OVERLAY_FONT does not exist: " + std::string(fromEnv));
    }

    std::error_code ec;

#ifdef _WIN32
    const std::filesystem::path fonts = SystemFontDir();

    for (const char* name : kFontNames)
    {
        const std::filesystem::path candidate = fonts / name;

        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }

    return ScanForFont(fonts);
#else
    for (const char* candidate : kFontCandidates)
    {
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }

    if (const char* home = std::getenv("HOME"); home && *home)
    {
        if (std::filesystem::path found = ScanForFont(std::filesystem::path(home) / ".local/share/fonts"); !found.empty())
            return found;
    }

    return ScanForFont("/usr/share/fonts");
#endif
}

namespace
{
struct Glyph
{
    std::vector<unsigned char> coverage;
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    int advance = 0;
};

void BlendPixel(unsigned char* pixel, float alpha, const cv::Scalar& tint)
{
    const float keep = 1.0f - alpha;

    for (int channel = 0; channel < 3; ++channel)
    {
        const float premultiplied = static_cast<float>(tint[channel]) * alpha;
        pixel[channel] = static_cast<unsigned char>(std::lround(premultiplied + pixel[channel] * keep));
    }

    pixel[3] = static_cast<unsigned char>(std::lround(255.0f * alpha + pixel[3] * keep));
}

void BlendGlyph(cv::Mat& canvas, const Glyph& glyph, int penX, int penY, const cv::Scalar& tint)
{
    if (glyph.coverage.empty())
        return;

    for (int row = 0; row < glyph.height; ++row)
    {
        const int y = penY + glyph.top + row;

        if (y < 0 || y >= canvas.rows)
            continue;

        unsigned char* line = canvas.ptr<unsigned char>(y);
        const unsigned char* source = glyph.coverage.data() + static_cast<std::size_t>(row) * glyph.width;

        for (int column = 0; column < glyph.width; ++column)
        {
            const int x = penX + glyph.left + column;

            if (x < 0 || x >= canvas.cols || source[column] == 0)
                continue;

            BlendPixel(line + static_cast<std::size_t>(x) * 4, source[column] / 255.0f, tint);
        }
    }
}

void BlendOutline(cv::Mat& canvas, const Glyph& glyph, int penX, int penY)
{
    const cv::Scalar black(0, 0, 0, 255);

    for (int dy = -kOutlineRadius; dy <= kOutlineRadius; ++dy)
    {
        for (int dx = -kOutlineRadius; dx <= kOutlineRadius; ++dx)
        {
            if ((dx != 0 || dy != 0) && dx * dx + dy * dy <= kOutlineRadius * kOutlineRadius)
                BlendGlyph(canvas, glyph, penX + dx, penY + dy, black);
        }
    }
}

cv::Mat Premultiplied(const cv::Mat& bgra)
{
    std::vector<cv::Mat> channels;
    cv::split(bgra, channels);

    for (int channel = 0; channel < 3; ++channel)
        cv::multiply(channels[channel], channels[3], channels[channel], 1.0 / 255.0);

    cv::Mat premultiplied;
    cv::merge(channels, premultiplied);

    return premultiplied;
}

void BlendImage(cv::Mat& canvas, const cv::Mat& image, int left, int top)
{
    for (int row = 0; row < image.rows; ++row)
    {
        const int y = top + row;

        if (y < 0 || y >= canvas.rows)
            continue;

        unsigned char* line = canvas.ptr<unsigned char>(y);
        const unsigned char* source = image.ptr<unsigned char>(row);

        for (int column = 0; column < image.cols; ++column)
        {
            const int x = left + column;
            const unsigned char* pixel = source + static_cast<std::size_t>(column) * 4;

            if (x < 0 || x >= canvas.cols || pixel[3] == 0)
                continue;

            unsigned char* target = line + static_cast<std::size_t>(x) * 4;
            const float keep = 1.0f - pixel[3] / 255.0f;

            for (int channel = 0; channel < 4; ++channel)
                target[channel] = static_cast<unsigned char>(std::lround(pixel[channel] + target[channel] * keep));
        }
    }
}
}

struct TextRaster::Impl
{
    std::vector<unsigned char> font;
    stbtt_fontinfo info{};
    bool ready = false;

    std::map<std::pair<int, char32_t>, Glyph> glyphs;

    std::map<char32_t, cv::Mat> iconSources;
    std::map<std::pair<int, char32_t>, cv::Mat> icons;
    bool iconsReady = false;

    struct FallbackFont
    {
        std::vector<unsigned char> data;
        stbtt_fontinfo info{};
    };

    std::map<FontScript, std::unique_ptr<FallbackFont>> fallbacks;

    const stbtt_fontinfo* FontFor(char32_t codepoint)
    {
        if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) != 0)
            return &info;

        FontScript script = FontScript::Japanese;

        if (codepoint >= 0x0E00 && codepoint <= 0x0E7F)
            script = FontScript::Thai;
        else if ((codepoint >= 0x1100 && codepoint <= 0x11FF) || (codepoint >= 0x3130 && codepoint <= 0x318F) || (codepoint >= 0xAC00 && codepoint <= 0xD7AF))
            script = FontScript::Korean;
        else if (codepoint < 0x3000 || codepoint > 0xFFEF)
            return &info;

        auto [it, inserted] = fallbacks.try_emplace(script);

        if (inserted)
        {
            for (const auto& candidate : FindScriptFonts())
            {
                if (candidate.script != script)
                    continue;

                auto face = std::make_unique<FallbackFont>();
                face->data = ReadFile(candidate.path);

                if (face->data.empty())
                    continue;

                const int offset = stbtt_GetFontOffsetForIndex(face->data.data(), candidate.index);

                if (offset >= 0 && stbtt_InitFont(&face->info, face->data.data(), offset))
                    it->second = std::move(face);
            }
        }

        if (it->second && stbtt_FindGlyphIndex(&it->second->info, static_cast<int>(codepoint)) != 0)
            return &it->second->info;

        return &info;
    }

    float capRatio = 0.0f;

    float ScaleFor(int pixelHeight)
    {
        if (capRatio <= 0.0f)
        {
            constexpr float kProbeHeight = 100.0f;
            const float probe = stbtt_ScaleForPixelHeight(&info, kProbeHeight);

            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetCodepointBitmapBox(&info, 'H', probe, probe, &x0, &y0, &x1, &y1);

            const int capHeight = y1 - y0;
            capRatio = capHeight > 0 ? static_cast<float>(capHeight) / kProbeHeight : 0.72f;
        }

        return stbtt_ScaleForPixelHeight(&info, static_cast<float>(pixelHeight) / capRatio);
    }

    const Glyph& GetGlyph(char32_t codepoint, int pixelHeight)
    {
        const auto key = std::make_pair(pixelHeight, codepoint);
        const auto it = glyphs.find(key);

        if (it != glyphs.end())
            return it->second;

        const stbtt_fontinfo* face = FontFor(codepoint);
        const float scale = face == &info ? ScaleFor(pixelHeight) : stbtt_ScaleForPixelHeight(face, pixelHeight * 1.4f);

        Glyph glyph;

        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(face, static_cast<int>(codepoint), &advance, &bearing);
        glyph.advance = static_cast<int>(std::lround(advance * scale));

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(face, static_cast<int>(codepoint), scale, scale, &x0, &y0, &x1, &y1);

        glyph.width = x1 - x0;
        glyph.height = y1 - y0;
        glyph.left = x0;
        glyph.top = y0;

        if (glyph.width > 0 && glyph.height > 0)
        {
            glyph.coverage.assign(static_cast<std::size_t>(glyph.width) * static_cast<std::size_t>(glyph.height), 0);
            stbtt_MakeCodepointBitmap(
                face,
                glyph.coverage.data(),
                glyph.width,
                glyph.height,
                glyph.width,
                scale,
                scale,
                static_cast<int>(codepoint)
            );
        }

        return glyphs.emplace(key, std::move(glyph)).first->second;
    }

    void LoadIcons()
    {
        iconsReady = true;

        for (const OverlayIcon& icon : kOverlayIcons)
        {
            const cv::Mat image = LoadIconImage(icon);

            if (image.empty())
            {
                iconsReady = false;
                continue;
            }

            iconSources[icon.codePoint] = Premultiplied(image);
        }
    }

    std::u32string CodePoints(const std::string& utf8) const
    {
        if (iconsReady)
            return DecodeUtf8(utf8);

        return DecodeUtf8(WithIconLabels(utf8));
    }

    const cv::Mat& GetIcon(char32_t codepoint, int pixelHeight)
    {
        const auto key = std::make_pair(pixelHeight, codepoint);
        const auto it = icons.find(key);

        if (it != icons.end())
            return it->second;

        const cv::Mat& source = iconSources.at(codepoint);
        const int height = std::max(1, static_cast<int>(std::lround(pixelHeight * kIconHeightPerCap)));
        const int width = std::max(1, static_cast<int>(std::lround(static_cast<double>(source.cols) * height / source.rows)));

        cv::Mat scaled;
        cv::resize(source, scaled, cv::Size(width, height), 0.0, 0.0, cv::INTER_AREA);

        return icons.emplace(key, std::move(scaled)).first->second;
    }

    int Kerning(char32_t first, char32_t second, float scale)
    {
        if (stbtt_FindGlyphIndex(&info, static_cast<int>(first)) == 0 || stbtt_FindGlyphIndex(&info, static_cast<int>(second)) == 0)
            return 0;

        const int kern = stbtt_GetCodepointKernAdvance(&info, static_cast<int>(first), static_cast<int>(second));

        return static_cast<int>(std::lround(kern * scale));
    }

    void VerticalMetrics(int pixelHeight, int& ascentPixels, int& descentPixels)
    {
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

        const float scale = ScaleFor(pixelHeight);

        ascentPixels = static_cast<int>(std::lround(ascent * scale));
        descentPixels = static_cast<int>(std::lround(-descent * scale));
    }
};

TextRaster::TextRaster() : impl_(std::make_unique<Impl>())
{
    const std::filesystem::path path = FindSystemFont();

    if (path.empty())
    {
        LOG_ERROR("Overlay text falls back to the stroke font: no TrueType font was found in the system font folders");
        return;
    }

    impl_->font = ReadFile(path);

    if (impl_->font.empty())
    {
        LOG_ERROR("Overlay text falls back to the stroke font: could not read " + path.string());
        return;
    }

    const int offset = stbtt_GetFontOffsetForIndex(impl_->font.data(), 0);

    if (offset < 0 || !stbtt_InitFont(&impl_->info, impl_->font.data(), offset))
    {
        LOG_ERROR("Overlay text falls back to the stroke font: could not parse " + path.string());
        impl_->font.clear();
        return;
    }

    impl_->ready = true;
    impl_->LoadIcons();
    LOG_INFO("Overlay font: " + path.string());
}

TextRaster::~TextRaster() = default;

TextRaster& TextRaster::Instance()
{
    static TextRaster raster;
    return raster;
}

bool TextRaster::Ready() const
{
    return impl_->ready;
}

int TextRaster::Descent(int pixelHeight)
{
    if (!impl_->ready)
        return 0;

    int ascent = 0;
    int descent = 0;
    impl_->VerticalMetrics(std::clamp(pixelHeight, kMinPixelHeight, kMaxPixelHeight), ascent, descent);

    return descent;
}

cv::Size TextRaster::Measure(const std::string& utf8, int pixelHeight)
{
    if (!impl_->ready)
        return {};

    const int height = std::clamp(pixelHeight, kMinPixelHeight, kMaxPixelHeight);
    const float scale = impl_->ScaleFor(height);
    const std::u32string points = impl_->CodePoints(utf8);

    int width = 0;

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (FindOverlayIcon(points[i]))
        {
            width += impl_->GetIcon(points[i], height).cols;
            continue;
        }

        width += impl_->GetGlyph(points[i], height).advance;

        if (i + 1 < points.size())
            width += impl_->Kerning(points[i], points[i + 1], scale);
    }

    int ascent = 0;
    int descent = 0;
    impl_->VerticalMetrics(height, ascent, descent);

    return cv::Size(width, ascent);
}

void TextRaster::Draw(
    cv::Mat& canvas,
    const std::string& utf8,
    const cv::Point& baseline,
    int pixelHeight,
    const cv::Scalar& color,
    bool outline
)
{
    if (!impl_->ready || canvas.empty() || canvas.type() != CV_8UC4)
        return;

    const int height = std::clamp(pixelHeight, kMinPixelHeight, kMaxPixelHeight);
    const float scale = impl_->ScaleFor(height);
    const std::u32string points = impl_->CodePoints(utf8);

    for (int pass = outline ? 0 : 1; pass < 2; ++pass)
    {
        int pen = baseline.x;

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if (FindOverlayIcon(points[i]))
            {
                const cv::Mat& icon = impl_->GetIcon(points[i], height);
                if (pass == 1)
                    BlendImage(canvas, icon, pen, baseline.y - (height + icon.rows) / 2);
                pen += icon.cols;
                continue;
            }

            const Glyph& glyph = impl_->GetGlyph(points[i], height);

            if (pass == 0)
                BlendOutline(canvas, glyph, pen, baseline.y);
            else
                BlendGlyph(canvas, glyph, pen, baseline.y, color);

            pen += glyph.advance;

            if (i + 1 < points.size())
                pen += impl_->Kerning(points[i], points[i + 1], scale);
        }
    }
}
