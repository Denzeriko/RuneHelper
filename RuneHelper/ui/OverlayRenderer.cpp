#include "ui/OverlayRenderer.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <string>

#include <opencv2/imgproc.hpp>

#include "ui/OverlayIcons.h"
#include "ui/TextRaster.h"
#include "common/Text.h"

namespace
{
constexpr int kMarkThickness = 2;
constexpr int kOutlineExtraThickness = 4;
constexpr int kTextPadding = 6;
constexpr int kBackdropAlpha = 208;
constexpr std::size_t kPanelLayoutCacheSize = 8;

struct TextLayout
{
    cv::Size size;
    int descent = 0;
    double fontScale = 1.0;
    int thickness = 1;
    bool trueType = false;
};

struct PanelLayout
{
    int width = 0;
    bool trueType = false;
    std::vector<OverlayPanelLine> lines;
    OverlayState state;
    int padding = 0;
    int step = 0;

    int Height() const { return 2 * padding + static_cast<int>(state.texts.size()) * step; }
};

cv::Scalar ToScalar(OverlayColor color, int alpha)
{
    const int r = static_cast<int>(color & 0xff);
    const int g = static_cast<int>((color >> 8) & 0xff);
    const int b = static_cast<int>((color >> 16) & 0xff);

    return cv::Scalar(b, g, r, alpha);
}

int Scaled(int pixels, const OverlayState& state)
{
    return std::max(1, static_cast<int>(std::lround(pixels * state.scale)));
}

int PixelHeight(const OverlayState& state)
{
    return std::max(8, Scaled(state.fontSize, state));
}

TextLayout Measure(const OverlayText& text, const OverlayState& state)
{
    TextLayout layout;

    const int pixelHeight = PixelHeight(state);
    TextRaster& raster = TextRaster::Instance();

    layout.trueType = raster.Ready();

    if (layout.trueType)
    {
        layout.size = raster.Measure(text.text, pixelHeight);
        layout.descent = raster.Descent(pixelHeight);

        return layout;
    }

    layout.thickness = std::max(1, pixelHeight / 16);
    layout.fontScale = cv::getFontScaleFromHeight(cv::FONT_HERSHEY_SIMPLEX, pixelHeight, layout.thickness);
    layout.size =
        cv::getTextSize(WithIconLabels(text.text), cv::FONT_HERSHEY_SIMPLEX, layout.fontScale, layout.thickness, &layout.descent);

    return layout;
}

cv::Rect BackdropBox(const OverlayText& text, const TextLayout& layout, const OverlayState& state)
{
    const int pad = Scaled(kTextPadding + (state.outline ? kOutlineExtraThickness : 0), state);
    const int baselineY = text.y + layout.size.height / 2;

    return cv::Rect(
        text.x - pad,
        baselineY - layout.size.height - pad,
        std::max(1, layout.size.width + 2 * pad),
        std::max(1, layout.size.height + layout.descent + 2 * pad)
    );
}

cv::Rect PreviewBox(const OverlayState& state)
{
    return cv::Rect(
        static_cast<int>(state.previewRect.left),
        static_cast<int>(state.previewRect.top),
        static_cast<int>(state.previewRect.right - state.previewRect.left),
        static_cast<int>(state.previewRect.bottom - state.previewRect.top)
    );
}

void Merge(cv::Rect& bounds, const cv::Rect& box)
{
    if (box.empty())
        return;

    bounds = bounds.empty() ? box : (bounds | box);
}

void PaintTexts(cv::Mat& canvas, const cv::Point& origin, const OverlayState& state)
{
    const cv::Rect canvasBounds(0, 0, canvas.cols, canvas.rows);

    for (const OverlayText& text : state.texts)
    {
        const TextLayout layout = Measure(text, state);
        const cv::Point baseline(text.x - origin.x, text.y - origin.y + layout.size.height / 2);

        if (state.background)
        {
            const cv::Rect backdrop = (BackdropBox(text, layout, state) - origin) & canvasBounds;

            if (!backdrop.empty())
                canvas(backdrop).setTo(cv::Scalar(0, 0, 0, kBackdropAlpha));
        }

        if (layout.trueType)
        {
            TextRaster::Instance().Draw(canvas, text.text, baseline, PixelHeight(state), ToScalar(text.color, 255), state.outline);

            continue;
        }

        const std::string labelled = WithIconLabels(text.text);

        if (state.outline)
        {
            cv::putText(
                canvas,
                labelled,
                baseline,
                cv::FONT_HERSHEY_SIMPLEX,
                layout.fontScale,
                cv::Scalar(0, 0, 0, 255),
                layout.thickness + Scaled(kOutlineExtraThickness, state),
                cv::LINE_AA
            );
        }

        cv::putText(
            canvas,
            labelled,
            baseline,
            cv::FONT_HERSHEY_SIMPLEX,
            layout.fontScale,
            ToScalar(text.color, 255),
            layout.thickness,
            cv::LINE_AA
        );
    }
}

PanelLayout LayoutPanel(const OverlayPanel& panel, float scale)
{
    PanelLayout result;
    result.width = panel.width;
    result.trueType = TextRaster::Instance().Ready();
    result.lines = panel.lines;
    OverlayState& layout = result.state;
    layout.fontSize = panel.fontSize;
    layout.scale = scale;
    layout.background = false;

    const int padding = Scaled(10, layout);
    const int width = std::max(1, panel.width - 2 * padding);
    const auto metrics = Measure({ "Ag", 0, 0 }, layout);
    const int step = metrics.size.height + metrics.descent + Scaled(8, layout);
    result.padding = padding;
    result.step = step;

    for (const auto& line : panel.lines)
    {
        auto remaining = Trim(line.text);

        while (!remaining.empty())
        {
            std::size_t fit = 0;
            std::size_t space = 0;

            for (std::size_t end = 0; end < remaining.size();)
            {
                const auto start = end;
                DecodeUtf8(remaining, end);

                if (Measure({ std::string(remaining.substr(0, end)), 0, 0 }, layout).size.width > width)
                    break;

                fit = end;

                if (remaining[start] == ' ')
                    space = start;
            }

            if (fit == 0)
                return result;

            if (fit < remaining.size() && space > 0)
                fit = space;

            const int y = padding + metrics.size.height / 2 + static_cast<int>(layout.texts.size()) * step;
            layout.texts.push_back({ std::string(Trim(remaining.substr(0, fit))), padding, y, line.color });
            remaining = Trim(remaining.substr(fit));
        }
    }

    return result;
}

const PanelLayout& CachedPanelLayout(const OverlayPanel& panel, float scale)
{
    thread_local std::list<PanelLayout> cache;
    const bool trueType = TextRaster::Instance().Ready();
    const auto found = std::find_if(
        cache.begin(),
        cache.end(),
        [&](const PanelLayout& entry)
        {
            return entry.width == panel.width && entry.state.fontSize == panel.fontSize && entry.state.scale == scale &&
                   entry.trueType == trueType && entry.lines == panel.lines;
        }
    );

    if (found != cache.end())
        cache.splice(cache.begin(), cache, found);
    else
    {
        cache.push_front(LayoutPanel(panel, scale));
        if (cache.size() > kPanelLayoutCacheSize)
            cache.pop_back();
    }

    return cache.front();
}
}

cv::Rect OverlayRenderer::ContentBounds(const OverlayState& state)
{
    cv::Rect bounds;

    for (const OverlayText& text : state.texts)
        Merge(bounds, BackdropBox(text, Measure(text, state), state));

    for (const OverlayMark& mark : state.marks)
        Merge(bounds, cv::Rect(mark.x, mark.y, mark.width, mark.height));

    for (const OverlayPanel& panel : state.panels)
        Merge(bounds, cv::Rect(panel.x, panel.y, panel.width, panel.height));

    if (state.previewEnabled)
        Merge(bounds, PreviewBox(state));

    return bounds;
}

int OverlayRenderer::PanelContentWidth(const OverlayPanel& panel, float scale)
{
    OverlayState state;
    state.fontSize = panel.fontSize;
    state.scale = scale;

    int width = 0;
    for (const auto& line : panel.lines)
        width = std::max(width, Measure({ std::string(Trim(line.text)), 0, 0 }, state).size.width);

    return width + 2 * Scaled(10, state);
}

int OverlayRenderer::PanelContentHeight(const OverlayPanel& panel, float scale)
{
    return CachedPanelLayout(panel, scale).Height();
}

void OverlayRenderer::Paint(cv::Mat& canvas, const cv::Point& origin, const OverlayState& state)
{
    if (canvas.empty() || canvas.type() != CV_8UC4)
        return;

    const cv::Rect canvasBounds(0, 0, canvas.cols, canvas.rows);

    PaintTexts(canvas, origin, state);

    for (const OverlayMark& mark : state.marks)
    {
        const cv::Rect box(mark.x - origin.x, mark.y - origin.y, std::max(1, mark.width - 1), std::max(1, mark.height - 1));

        if ((box & canvasBounds).empty())
            continue;

        cv::rectangle(canvas, box, ToScalar(mark.color, 255), Scaled(kMarkThickness, state), cv::LINE_AA);
    }

    for (const OverlayPanel& panel : state.panels)
    {
        const cv::Rect bounds = (cv::Rect(panel.x, panel.y, panel.width, panel.height) - origin) & canvasBounds;

        if (bounds.empty())
            continue;

        cv::Mat card = canvas(bounds);
        if (panel.background)
            card.setTo(cv::Scalar(0, 0, 0, kBackdropAlpha));
        const PanelLayout& cached = CachedPanelLayout(panel, state.scale);
        OverlayState layout = cached.state;
        layout.outline = panel.outline;
        const auto rows = static_cast<std::size_t>(std::max(1, (panel.height - 2 * cached.padding) / cached.step));
        if (layout.texts.size() > rows)
        {
            layout.texts.resize(rows);
            layout.texts.back().text = "More in Maps...";
        }
        PaintTexts(card, origin + bounds.tl() - cv::Point(panel.x, panel.y), layout);
    }

    if (!state.previewEnabled)
        return;

    const cv::Rect preview = PreviewBox(state);

    if (preview.empty())
        return;

    const cv::Rect box(preview.x - origin.x, preview.y - origin.y, std::max(1, preview.width - 1), std::max(1, preview.height - 1));

    if ((box & canvasBounds).empty())
        return;

    cv::rectangle(canvas, box, ToScalar(OverlayRgb(0, 255, 0), 255), Scaled(kMarkThickness, state), cv::LINE_AA);
}
