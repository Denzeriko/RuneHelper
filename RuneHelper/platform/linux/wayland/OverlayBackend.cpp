#include "platform/OverlayBackend.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "WaylandSession.h"
#include "core/Logger.h"
#include "ui/OverlayState.h"

namespace
{
constexpr int kBufferSlots = 2;
constexpr int kTextPadding = 6;
constexpr int kDirtyMargin = 2;

std::string ToNarrow(const std::wstring& text)
{
    std::string result;
    result.reserve(text.size());

    for (wchar_t ch : text)
        result.push_back(ch >= 0 && ch <= 127 ? static_cast<char>(ch) : '?');

    return result;
}

cv::Scalar ToScalar(OverlayColor color, int alpha)
{
    const int r = static_cast<int>(color & 0xff);
    const int g = static_cast<int>((color >> 8) & 0xff);
    const int b = static_cast<int>((color >> 16) & 0xff);

    return cv::Scalar(b, g, r, alpha);
}

cv::Rect UnionRect(const cv::Rect& a, const cv::Rect& b)
{
    if (a.empty())
        return b;

    if (b.empty())
        return a;

    return a | b;
}

bool SameRect(const OverlayRect& a, const OverlayRect& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool SameTexts(const std::vector<OverlayText>& left, const std::vector<OverlayText>& right)
{
    if (left.size() != right.size())
        return false;

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (left[i].x != right[i].x || left[i].y != right[i].y ||
            left[i].color != right[i].color || left[i].text != right[i].text)
        {
            return false;
        }
    }

    return true;
}

class WaylandOverlayBackend final : public OverlayBackend
{
public:
    bool Init(const char* title, int width, int height) override;
    void Shutdown() override;

    bool IsRunning() const override;
    void PumpEvents() override;
    void Render(const OverlayState& state) override;

    void SetVisible(bool visible) override;
    void SetClickThrough(bool enabled) override;
    void SetAlwaysOnTop(bool enabled) override;
    void BringToTop() override;

private:
    static void HandleConfigure(void* data, zwlr_layer_surface_v1* surface, std::uint32_t serial, std::uint32_t width, std::uint32_t height);
    static void HandleClosed(void* data, zwlr_layer_surface_v1* surface);
    static void HandleBufferRelease(void* data, wl_buffer* buffer);

    bool CreateSurface(const WaylandOutput* output);
    void DestroySurface();
    void Hide();
    void ApplyInputRegion();
    void EnsureConfigured();
    void Draw();

    const WaylandOutput* CurrentOutput() const;
    cv::Rect ComputeContentRect(double& fontScale, int& thickness) const;
    cv::Rect PreviewRect() const;
    int AcquireBuffer(int width, int height);

    WaylandSession session_;
    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layerSurface_ = nullptr;
    std::uint32_t outputName_ = 0;
    bool hasOutput_ = false;
    WaylandShmBuffer buffers_[kBufferSlots];
    bool busy_[kBufferSlots] = {false, false};

    OverlayState state_;
    std::vector<OverlayText> drawnTexts_;
    cv::Rect surfaceRect_;
    cv::Mat canvas_;
    cv::Rect canvasRect_;
    cv::Rect presentedRect_;
    bool needsRedraw_ = true;
    cv::Rect slotRect_[kBufferSlots];
    OverlayRect drawnPreviewRect_{};
    bool drawnPreview_ = false;
    int drawnFontSize_ = 0;

    bool running_ = false;
    bool visible_ = false;
    bool mapped_ = false;
    bool clickThrough_ = true;
    bool alwaysOnTop_ = true;
    bool configured_ = false;
    std::string namespace_ = "runehelper";

    static const zwlr_layer_surface_v1_listener kLayerSurfaceListener;
    static const wl_buffer_listener kBufferListener;
};

const zwlr_layer_surface_v1_listener WaylandOverlayBackend::kLayerSurfaceListener = {
    &WaylandOverlayBackend::HandleConfigure,
    &WaylandOverlayBackend::HandleClosed
};

const wl_buffer_listener WaylandOverlayBackend::kBufferListener = {
    &WaylandOverlayBackend::HandleBufferRelease
};

void WaylandOverlayBackend::HandleConfigure(void* data, zwlr_layer_surface_v1* surface, std::uint32_t serial, std::uint32_t width, std::uint32_t height)
{
    auto* backend = static_cast<WaylandOverlayBackend*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    if (width > 0 && height > 0)
    {
        backend->surfaceRect_.width = static_cast<int>(width);
        backend->surfaceRect_.height = static_cast<int>(height);
    }

    backend->configured_ = true;
    backend->needsRedraw_ = true;
}

void WaylandOverlayBackend::HandleClosed(void* data, zwlr_layer_surface_v1*)
{
    auto* backend = static_cast<WaylandOverlayBackend*>(data);
    backend->configured_ = false;
    backend->mapped_ = false;
    backend->needsRedraw_ = true;
}

void WaylandOverlayBackend::HandleBufferRelease(void* data, wl_buffer* buffer)
{
    auto* backend = static_cast<WaylandOverlayBackend*>(data);

    for (int i = 0; i < kBufferSlots; ++i)
    {
        if (backend->buffers_[i].Buffer() == buffer)
            backend->busy_[i] = false;
    }
}

bool WaylandOverlayBackend::Init(const char* title, int, int)
{
    if (title && *title)
        namespace_ = title;

    if (!session_.Connect())
        return true;

    if (!session_.LayerShell())
    {
        LOG_ERROR("Wayland overlay requires zwlr_layer_shell_v1, which this compositor does not support");
        return true;
    }

    running_ = true;
    visible_ = true;
    LOG_INFO("Wayland layer-shell overlay backend initialized");
    return true;
}

bool WaylandOverlayBackend::CreateSurface(const WaylandOutput* output)
{
    if (!output)
        return false;

    surface_ = wl_compositor_create_surface(session_.Compositor());

    if (!surface_)
    {
        LOG_ERROR("Wayland overlay failed to create a surface");
        return false;
    }

    layerSurface_ = zwlr_layer_shell_v1_get_layer_surface(
        session_.LayerShell(),
        surface_,
        output->output,
        alwaysOnTop_ ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY : ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        namespace_.c_str()
    );

    if (!layerSurface_)
    {
        LOG_ERROR("Wayland overlay failed to create a layer surface");
        wl_surface_destroy(surface_);
        surface_ = nullptr;
        return false;
    }

    outputName_ = output->globalName;
    hasOutput_ = true;
    configured_ = false;
    mapped_ = false;
    needsRedraw_ = true;
    surfaceRect_ = cv::Rect(output->x, output->y, output->LogicalWidth(), output->LogicalHeight());

    zwlr_layer_surface_v1_add_listener(layerSurface_, &kLayerSurfaceListener, this);
    zwlr_layer_surface_v1_set_anchor(
        layerSurface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
    );
    zwlr_layer_surface_v1_set_size(layerSurface_, 0, 0);
    zwlr_layer_surface_v1_set_margin(layerSurface_, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layerSurface_, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layerSurface_, -1);

    ApplyInputRegion();
    return true;
}

void WaylandOverlayBackend::DestroySurface()
{
    for (int i = 0; i < kBufferSlots; ++i)
    {
        buffers_[i].Destroy();
        busy_[i] = false;
        slotRect_[i] = cv::Rect();
    }

    canvasRect_ = cv::Rect();
    presentedRect_ = cv::Rect();

    if (layerSurface_)
    {
        zwlr_layer_surface_v1_destroy(layerSurface_);
        layerSurface_ = nullptr;
    }

    if (surface_)
    {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }

    session_.Flush();

    configured_ = false;
    mapped_ = false;
    outputName_ = 0;
    hasOutput_ = false;
    surfaceRect_ = cv::Rect();
    needsRedraw_ = true;
}

void WaylandOverlayBackend::Shutdown()
{
    running_ = false;
    visible_ = false;

    DestroySurface();
    session_.Disconnect();
}

bool WaylandOverlayBackend::IsRunning() const
{
    return running_;
}

void WaylandOverlayBackend::PumpEvents()
{
    if (!running_)
        return;

    session_.DispatchNonBlocking();
    session_.Flush();
}

void WaylandOverlayBackend::Hide()
{
    if (!surface_ || !mapped_)
        return;

    wl_surface_attach(surface_, nullptr, 0, 0);
    wl_surface_commit(surface_);
    session_.Flush();

    mapped_ = false;
    configured_ = false;
    needsRedraw_ = true;
}

void WaylandOverlayBackend::ApplyInputRegion()
{
    if (!surface_)
        return;

    if (clickThrough_)
    {
        wl_region* region = wl_compositor_create_region(session_.Compositor());
        wl_surface_set_input_region(surface_, region);
        wl_region_destroy(region);
    }
    else
    {
        wl_surface_set_input_region(surface_, nullptr);
    }
}

void WaylandOverlayBackend::EnsureConfigured()
{
    if (!layerSurface_ || !surface_ || configured_)
        return;

    wl_surface_commit(surface_);
    session_.Roundtrip();
}

const WaylandOutput* WaylandOverlayBackend::CurrentOutput() const
{
    if (!hasOutput_)
        return nullptr;

    return session_.OutputByName(outputName_);
}

cv::Rect WaylandOverlayBackend::PreviewRect() const
{
    return cv::Rect(
        static_cast<int>(state_.previewRect.left),
        static_cast<int>(state_.previewRect.top),
        static_cast<int>(state_.previewRect.right - state_.previewRect.left),
        static_cast<int>(state_.previewRect.bottom - state_.previewRect.top)
    );
}

cv::Rect WaylandOverlayBackend::ComputeContentRect(double& fontScale, int& thickness) const
{
    thickness = std::max(1, state_.fontSize / 16);
    fontScale = cv::getFontScaleFromHeight(cv::FONT_HERSHEY_SIMPLEX, std::max(8, state_.fontSize), thickness);

    cv::Rect bounds;

    for (const OverlayText& text : state_.texts)
    {
        int baseline = 0;
        const cv::Size size = cv::getTextSize(ToNarrow(text.text), cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);
        const cv::Rect box(
            text.x - kTextPadding,
            text.y - size.height / 2 - kTextPadding,
            size.width + 2 * kTextPadding,
            size.height + baseline + 2 * kTextPadding
        );

        bounds = bounds.empty() ? box : (bounds | box);
    }

    if (state_.previewEnabled)
    {
        const cv::Rect preview = PreviewRect();

        if (!preview.empty())
            bounds = bounds.empty() ? preview : (bounds | preview);
    }

    return bounds;
}

int WaylandOverlayBackend::AcquireBuffer(int width, int height)
{
    const int stride = width * 4;

    for (int i = 0; i < kBufferSlots; ++i)
    {
        if (busy_[i])
            continue;

        WaylandShmBuffer& buffer = buffers_[i];

        if (buffer.IsValid() && buffer.Width() == width && buffer.Height() == height)
            return i;

        if (!buffer.Create(session_.Shm(), width, height, stride, WL_SHM_FORMAT_ARGB8888))
            return -1;

        wl_buffer_add_listener(buffer.Buffer(), &kBufferListener, this);
        slotRect_[i] = cv::Rect();

        return i;
    }

    return -1;
}

void WaylandOverlayBackend::Draw()
{
    if (!surface_ || !configured_ || surfaceRect_.empty())
        return;

    const int slot = AcquireBuffer(surfaceRect_.width, surfaceRect_.height);

    if (slot < 0)
        return;

    WaylandShmBuffer& buffer = buffers_[slot];
    const bool fullDamage = needsRedraw_;

    if (canvas_.rows != surfaceRect_.height || canvas_.cols != surfaceRect_.width)
    {
        canvas_.create(surfaceRect_.height, surfaceRect_.width, CV_8UC4);
        canvas_.setTo(cv::Scalar(0, 0, 0, 0));
        canvasRect_ = cv::Rect();
    }

    cv::Mat& canvas = canvas_;
    const cv::Rect surfaceBounds(0, 0, surfaceRect_.width, surfaceRect_.height);

    int thickness = 1;
    double fontScale = 1.0;
    const cv::Rect content = ComputeContentRect(fontScale, thickness);

    cv::Rect localContent;

    if (!content.empty())
    {
        localContent = cv::Rect(
            content.x - surfaceRect_.x - kDirtyMargin,
            content.y - surfaceRect_.y - kDirtyMargin,
            content.width + 2 * kDirtyMargin,
            content.height + 2 * kDirtyMargin
        ) & surfaceBounds;
    }

    const cv::Rect clearRect = UnionRect(canvasRect_, localContent) & surfaceBounds;

    if (!clearRect.empty())
        canvas(clearRect).setTo(cv::Scalar(0, 0, 0, 0));

    if (!state_.texts.empty())
    {
        for (const OverlayText& text : state_.texts)
        {
            const std::string narrow = ToNarrow(text.text);
            int baseline = 0;
            const cv::Size size = cv::getTextSize(narrow, cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);

            const cv::Point origin(text.x - surfaceRect_.x, text.y - surfaceRect_.y + size.height / 2);
            const cv::Rect backdrop(
                origin.x - kTextPadding,
                origin.y - size.height - kTextPadding,
                size.width + 2 * kTextPadding,
                size.height + baseline + 2 * kTextPadding
            );

            const cv::Rect clipped = backdrop & cv::Rect(0, 0, canvas.cols, canvas.rows);

            if (!clipped.empty())
                canvas(clipped).setTo(cv::Scalar(0, 0, 0, 208));

            cv::putText(canvas, narrow, origin, cv::FONT_HERSHEY_SIMPLEX, fontScale, ToScalar(text.color, 255), thickness, cv::LINE_AA);
        }
    }
    if (state_.previewEnabled)
    {
        const cv::Rect preview = PreviewRect();

        if (!preview.empty())
        {
            cv::rectangle(
                canvas,
                cv::Rect(
                    preview.x - surfaceRect_.x,
                    preview.y - surfaceRect_.y,
                    std::max(1, preview.width - 1),
                    std::max(1, preview.height - 1)
                ),
                ToScalar(OverlayRgb(0, 255, 0), 255),
                2
            );
        }
    }

    canvasRect_ = localContent;

    const cv::Rect copyRect = UnionRect(slotRect_[slot], localContent) & surfaceBounds;

    if (!copyRect.empty())
    {
        unsigned char* destination = buffer.Data();
        const std::size_t columnOffset = static_cast<std::size_t>(copyRect.x) * 4;
        const std::size_t rowBytes = static_cast<std::size_t>(copyRect.width) * 4;

        for (int row = copyRect.y; row < copyRect.y + copyRect.height; ++row)
        {
            std::memcpy(
                destination + static_cast<std::size_t>(row) * buffer.Stride() + columnOffset,
                canvas.ptr(row) + columnOffset,
                rowBytes
            );
        }
    }

    slotRect_[slot] = localContent;
    busy_[slot] = true;

    const cv::Rect damageRect = fullDamage
        ? surfaceBounds
        : (UnionRect(copyRect, presentedRect_) & surfaceBounds);

    wl_surface_attach(surface_, buffer.Buffer(), 0, 0);

    if (!damageRect.empty())
        wl_surface_damage_buffer(surface_, damageRect.x, damageRect.y, damageRect.width, damageRect.height);

    wl_surface_commit(surface_);
    session_.Flush();

    mapped_ = true;
    presentedRect_ = localContent;
    needsRedraw_ = false;
    drawnTexts_ = state_.texts;
    drawnPreview_ = state_.previewEnabled;
    drawnPreviewRect_ = state_.previewRect;
    drawnFontSize_ = state_.fontSize;
}

void WaylandOverlayBackend::Render(const OverlayState& state)
{
    if (!running_ || !session_.LayerShell())
        return;

    if (!visible_)
    {
        state_ = state;
        Hide();
        return;
    }

    if (!needsRedraw_ &&
        mapped_ &&
        drawnPreview_ == state.previewEnabled &&
        drawnFontSize_ == state.fontSize &&
        SameRect(drawnPreviewRect_, state.previewRect) &&
        SameTexts(drawnTexts_, state.texts))
    {
        return;
    }

    state_ = state;

    int thickness = 1;
    double fontScale = 1.0;
    cv::Rect content = ComputeContentRect(fontScale, thickness);

    const WaylandOutput* current = CurrentOutput();
    const WaylandOutput* target = nullptr;

    if (current && (content.empty() ||
        !(content & cv::Rect(current->x, current->y, current->LogicalWidth(), current->LogicalHeight())).empty()))
    {
        target = current;
    }

    if (!target && !content.empty())
        target = session_.OutputAt(content.x, content.y);

    if (!target)
        target = session_.PrimaryOutput();

    if (!target)
    {
        Hide();
        return;
    }

    if (!content.empty())
        content &= cv::Rect(target->x, target->y, target->LogicalWidth(), target->LogicalHeight());

    if (!surface_ || current != target)
    {
        DestroySurface();

        if (!CreateSurface(target))
            return;
    }

    if (!mapped_)
        EnsureConfigured();

    Draw();
}

void WaylandOverlayBackend::SetVisible(bool visible)
{
    if (visible_ == visible)
        return;

    visible_ = visible;

    if (!visible_)
        Hide();
    else
        needsRedraw_ = true;
}

void WaylandOverlayBackend::SetClickThrough(bool enabled)
{
    if (clickThrough_ == enabled)
        return;

    clickThrough_ = enabled;
    ApplyInputRegion();

    if (surface_)
    {
        wl_surface_commit(surface_);
        session_.Flush();
    }
}

void WaylandOverlayBackend::SetAlwaysOnTop(bool enabled)
{
    if (alwaysOnTop_ == enabled)
        return;

    alwaysOnTop_ = enabled;

    if (!layerSurface_)
        return;

    if (zwlr_layer_surface_v1_get_version(layerSurface_) >= ZWLR_LAYER_SURFACE_V1_SET_LAYER_SINCE_VERSION)
    {
        zwlr_layer_surface_v1_set_layer(
            layerSurface_,
            alwaysOnTop_ ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY : ZWLR_LAYER_SHELL_V1_LAYER_TOP
        );

        wl_surface_commit(surface_);
        session_.Flush();
        return;
    }

    DestroySurface();
}

void WaylandOverlayBackend::BringToTop()
{
    if (!surface_ || !mapped_)
        return;

    wl_surface_commit(surface_);
    session_.Flush();
}
}

std::unique_ptr<OverlayBackend> CreateOverlayBackend()
{
    return std::make_unique<WaylandOverlayBackend>();
}
