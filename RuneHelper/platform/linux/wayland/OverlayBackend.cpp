#include "platform/OverlayBackend.h"

#include <algorithm>
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

    cv::Rect ComputeContentRect(double& fontScale, int& thickness) const;
    cv::Rect PreviewRect() const;
    WaylandShmBuffer* AcquireBuffer(int width, int height);

    WaylandSession session_;
    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layerSurface_ = nullptr;
    const WaylandOutput* output_ = nullptr;
    WaylandShmBuffer buffers_[kBufferSlots];
    bool busy_[kBufferSlots] = {false, false};

    OverlayState state_;
    std::vector<OverlayText> drawnTexts_;
    cv::Rect surfaceRect_;
    cv::Rect drawnContentRect_;
    cv::Mat canvas_;
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
    backend->drawnContentRect_ = cv::Rect();
}

void WaylandOverlayBackend::HandleClosed(void* data, zwlr_layer_surface_v1*)
{
    auto* backend = static_cast<WaylandOverlayBackend*>(data);
    backend->configured_ = false;
    backend->mapped_ = false;
    backend->drawnContentRect_ = cv::Rect();
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

    output_ = output;
    configured_ = false;
    mapped_ = false;
    drawnContentRect_ = cv::Rect();
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
    }

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
    output_ = nullptr;
    surfaceRect_ = cv::Rect();
    drawnContentRect_ = cv::Rect();
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
    drawnContentRect_ = cv::Rect();
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

WaylandShmBuffer* WaylandOverlayBackend::AcquireBuffer(int width, int height)
{
    const int stride = width * 4;

    for (int i = 0; i < kBufferSlots; ++i)
    {
        if (busy_[i])
            continue;

        WaylandShmBuffer& buffer = buffers_[i];

        if (buffer.IsValid() && buffer.Width() == width && buffer.Height() == height)
            return &buffer;

        if (!buffer.Create(session_.Shm(), width, height, stride, WL_SHM_FORMAT_ARGB8888))
            return nullptr;

        wl_buffer_add_listener(buffer.Buffer(), &kBufferListener, this);
        return &buffer;
    }

    return nullptr;
}

void WaylandOverlayBackend::Draw()
{
    if (!surface_ || !configured_ || surfaceRect_.empty())
        return;

    WaylandShmBuffer* buffer = AcquireBuffer(surfaceRect_.width, surfaceRect_.height);

    if (!buffer)
        return;

    if (canvas_.rows != surfaceRect_.height || canvas_.cols != surfaceRect_.width)
        canvas_.create(surfaceRect_.height, surfaceRect_.width, CV_8UC4);

    canvas_.setTo(cv::Scalar(0, 0, 0, 0));
    cv::Mat& canvas = canvas_;

    int thickness = 1;
    double fontScale = 1.0;
    const cv::Rect content = ComputeContentRect(fontScale, thickness);

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

    unsigned char* destination = buffer->Data();

    for (int row = 0; row < surfaceRect_.height; ++row)
    {
        std::memcpy(
            destination + static_cast<std::size_t>(row) * buffer->Stride(),
            canvas.ptr(row),
            static_cast<std::size_t>(surfaceRect_.width) * 4
        );
    }

    for (int i = 0; i < kBufferSlots; ++i)
    {
        if (buffers_[i].Buffer() == buffer->Buffer())
            busy_[i] = true;
    }

    cv::Rect damage = content.empty() ? drawnContentRect_
                                      : (drawnContentRect_.empty() ? content : (content | drawnContentRect_));
    damage &= surfaceRect_;

    wl_surface_attach(surface_, buffer->Buffer(), 0, 0);

    if (damage.empty())
        wl_surface_damage_buffer(surface_, 0, 0, surfaceRect_.width, surfaceRect_.height);
    else
        wl_surface_damage_buffer(surface_, damage.x - surfaceRect_.x, damage.y - surfaceRect_.y, damage.width, damage.height);

    wl_surface_commit(surface_);
    session_.Flush();

    mapped_ = true;
    drawnContentRect_ = content;
    drawnTexts_ = state_.texts;
    drawnPreview_ = state_.previewEnabled;
    drawnFontSize_ = state_.fontSize;
}

void WaylandOverlayBackend::Render(const OverlayState& state)
{
    if (!running_ || !session_.LayerShell())
        return;

    state_ = state;

    if (!visible_)
    {
        Hide();
        return;
    }

    int thickness = 1;
    double fontScale = 1.0;
    cv::Rect content = ComputeContentRect(fontScale, thickness);

    const WaylandOutput* target = nullptr;

    if (output_ && (content.empty() ||
        !(content & cv::Rect(output_->x, output_->y, output_->LogicalWidth(), output_->LogicalHeight())).empty()))
    {
        target = output_;
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

    if (!surface_ || output_ != target)
    {
        DestroySurface();

        if (!CreateSurface(target))
            return;
    }

    if (!mapped_)
        EnsureConfigured();

    const bool unchanged =
        mapped_ &&
        drawnContentRect_ == content &&
        drawnPreview_ == state_.previewEnabled &&
        drawnFontSize_ == state_.fontSize &&
        SameTexts(drawnTexts_, state_.texts);

    if (unchanged)
        return;

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
        drawnContentRect_ = cv::Rect();
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
