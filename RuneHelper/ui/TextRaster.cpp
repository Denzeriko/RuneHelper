#include "ui/TextRaster.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>
#include <vector>

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

#include "core/Logger.h"

namespace
{
constexpr int kMinPixelHeight = 6;
constexpr int kMaxPixelHeight = 256;

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

std::filesystem::path FindFont()
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

std::vector<std::uint32_t> DecodeUtf8(const std::string& text)
{
    std::vector<std::uint32_t> points;
    points.reserve(text.size());

    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::uint32_t point = lead;
        std::size_t extra = 0;

        if (lead >= 0xF0)
        {
            point = lead & 0x07u;
            extra = 3;
        }
        else if (lead >= 0xE0)
        {
            point = lead & 0x0Fu;
            extra = 2;
        }
        else if (lead >= 0xC0)
        {
            point = lead & 0x1Fu;
            extra = 1;
        }
        else if (lead >= 0x80)
        {
            ++i;
            continue;
        }

        if (i + extra >= text.size())
            break;

        for (std::size_t k = 1; k <= extra; ++k)
            point = (point << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);

        points.push_back(point);
        i += extra + 1;
    }

    return points;
}
}

struct TextRaster::Impl
{
    std::vector<unsigned char> font;
    stbtt_fontinfo info{};
    bool ready = false;

    struct Glyph
    {
        std::vector<unsigned char> coverage;
        int width = 0;
        int height = 0;
        int left = 0;
        int top = 0;
        int advance = 0;
    };

    std::map<std::pair<int, std::uint32_t>, Glyph> glyphs;

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

    const Glyph& GetGlyph(std::uint32_t codepoint, int pixelHeight)
    {
        const auto key = std::make_pair(pixelHeight, codepoint);
        const auto it = glyphs.find(key);

        if (it != glyphs.end())
            return it->second;

        const float scale = ScaleFor(pixelHeight);

        Glyph glyph;

        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&info, static_cast<int>(codepoint), &advance, &bearing);
        glyph.advance = static_cast<int>(std::lround(advance * scale));

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, static_cast<int>(codepoint), scale, scale, &x0, &y0, &x1, &y1);

        glyph.width = x1 - x0;
        glyph.height = y1 - y0;
        glyph.left = x0;
        glyph.top = y0;

        if (glyph.width > 0 && glyph.height > 0)
        {
            glyph.coverage.assign(static_cast<std::size_t>(glyph.width) * static_cast<std::size_t>(glyph.height), 0);
            stbtt_MakeCodepointBitmap(
                &info,
                glyph.coverage.data(),
                glyph.width,
                glyph.height,
                glyph.width,
                scale,
                scale,
                static_cast<int>(codepoint));
        }

        return glyphs.emplace(key, std::move(glyph)).first->second;
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

TextRaster::TextRaster()
    : impl_(std::make_unique<Impl>())
{
    const std::filesystem::path path = FindFont();

    if (path.empty())
    {
        LOG_ERROR("Overlay text falls back to the stroke font: no TrueType font was found under /usr/share/fonts");
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
    const std::vector<std::uint32_t> points = DecodeUtf8(utf8);

    int width = 0;

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        width += impl_->GetGlyph(points[i], height).advance;

        if (i + 1 < points.size())
        {
            const int kern = stbtt_GetCodepointKernAdvance(
                &impl_->info,
                static_cast<int>(points[i]),
                static_cast<int>(points[i + 1]));

            width += static_cast<int>(std::lround(kern * scale));
        }
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
    bool outline)
{
    if (!impl_->ready || canvas.empty() || canvas.type() != CV_8UC4)
        return;

    const int height = std::clamp(pixelHeight, kMinPixelHeight, kMaxPixelHeight);
    const float scale = impl_->ScaleFor(height);
    const std::vector<std::uint32_t> points = DecodeUtf8(utf8);

    auto blit = [&canvas](const Impl::Glyph& glyph, int penX, int penY, const cv::Scalar& tint)
    {
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

                if (x < 0 || x >= canvas.cols)
                    continue;

                const int coverage = source[column];

                if (coverage == 0)
                    continue;

                const float alpha = coverage / 255.0f;
                const float keep = 1.0f - alpha;
                unsigned char* pixel = line + static_cast<std::size_t>(x) * 4;

                for (int channel = 0; channel < 3; ++channel)
                {
                    const float premultiplied = static_cast<float>(tint[channel]) * alpha;
                    pixel[channel] = static_cast<unsigned char>(std::lround(premultiplied + pixel[channel] * keep));
                }

                pixel[3] = static_cast<unsigned char>(std::lround(255.0f * alpha + pixel[3] * keep));
            }
        }
    };

    const cv::Scalar black(0, 0, 0, 255);

    int pen = baseline.x;

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const Impl::Glyph& glyph = impl_->GetGlyph(points[i], height);

        if (!glyph.coverage.empty())
        {
            if (outline)
            {
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        if (dx != 0 || dy != 0)
                            blit(glyph, pen + dx, baseline.y + dy, black);
                    }
                }
            }

            blit(glyph, pen, baseline.y, color);
        }

        pen += glyph.advance;

        if (i + 1 < points.size())
        {
            const int kern = stbtt_GetCodepointKernAdvance(
                &impl_->info,
                static_cast<int>(points[i]),
                static_cast<int>(points[i + 1]));

            pen += static_cast<int>(std::lround(kern * scale));
        }
    }
}
