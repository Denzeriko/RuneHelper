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
    void UpdateGeometry();
    void Draw();

    cv::Rect ComputeContentRect(double& fontScale, int& thickness) const;
    WaylandShmBuffer* AcquireBuffer(int width, int height);

    WaylandSession session_;
    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layerSurface_ = nullptr;
    const WaylandOutput* output_ = nullptr;
    WaylandShmBuffer buffers_[kBufferSlots];
    bool busy_[kBufferSlots] = {false, false};

    OverlayState state_;
    std::vector<OverlayText> drawnTexts_;
    cv::Rect contentRect_;
    cv::Rect drawnRect_;
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
        backend->contentRect_.width = static_cast<int>(width);
        backend->contentRect_.height = static_cast<int>(height);
    }

    backend->configured_ = true;
    backend->drawnRect_ = cv::Rect();
}

void WaylandOverlayBackend::HandleClosed(void* data, zwlr_layer_surface_v1*)
{
    auto* backend = static_cast<WaylandOverlayBackend*>(data);
    backend->configured_ = false;
    backend->mapped_ = false;
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
    drawnRect_ = cv::Rect();

    zwlr_layer_surface_v1_add_listener(layerSurface_, &kLayerSurfaceListener, this);
    zwlr_layer_surface_v1_set_anchor(
        layerSurface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
    );
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
    contentRect_ = cv::Rect();
    drawnRect_ = cv::Rect();
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

    session_.DispatchPending();
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
    drawnRect_ = cv::Rect();
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

void WaylandOverlayBackend::UpdateGeometry()
{
    if (!layerSurface_ || !output_ || contentRect_.empty())
        return;

    zwlr_layer_surface_v1_set_size(
        layerSurface_,
        static_cast<std::uint32_t>(contentRect_.width),
        static_cast<std::uint32_t>(contentRect_.height)
    );

    zwlr_layer_surface_v1_set_margin(
        layerSurface_,
        contentRect_.y - output_->y,
        0,
        0,
        contentRect_.x - output_->x
    );

    configured_ = false;
    wl_surface_commit(surface_);
    session_.Roundtrip();
}

cv::Rect WaylandOverlayBackend::ComputeContentRect(double& fontScale, int& thickness) const
{
    thickness = std::max(1, state_.fontSize / 16);
    fontScale = cv::getFontScaleFromHeight(cv::FONT_HERSHEY_SIMPLEX, std::max(8, state_.fontSize), thickness);

    if (!state_.texts.empty())
    {
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

        return bounds;
    }

    if (state_.previewEnabled)
    {
        return cv::Rect(
            static_cast<int>(state_.previewRect.left),
            static_cast<int>(state_.previewRect.top),
            static_cast<int>(state_.previewRect.right - state_.previewRect.left),
            static_cast<int>(state_.previewRect.bottom - state_.previewRect.top)
        );
    }

    return {};
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
    if (!surface_ || !configured_ || contentRect_.empty())
        return;

    WaylandShmBuffer* buffer = AcquireBuffer(contentRect_.width, contentRect_.height);

    if (!buffer)
        return;

    cv::Mat canvas(contentRect_.height, contentRect_.width, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    int thickness = 1;
    double fontScale = 1.0;
    ComputeContentRect(fontScale, thickness);

    if (!state_.texts.empty())
    {
        for (const OverlayText& text : state_.texts)
        {
            const std::string narrow = ToNarrow(text.text);
            int baseline = 0;
            const cv::Size size = cv::getTextSize(narrow, cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);

            const cv::Point origin(text.x - contentRect_.x, text.y - contentRect_.y + size.height / 2);
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
    else if (state_.previewEnabled)
    {
        cv::rectangle(
            canvas,
            cv::Rect(0, 0, std::max(1, contentRect_.width - 1), std::max(1, contentRect_.height - 1)),
            ToScalar(OverlayRgb(0, 255, 0), 255),
            2
        );
    }

    unsigned char* destination = buffer->Data();

    for (int row = 0; row < contentRect_.height; ++row)
    {
        std::memcpy(
            destination + static_cast<std::size_t>(row) * buffer->Stride(),
            canvas.ptr(row),
            static_cast<std::size_t>(contentRect_.width) * 4
        );
    }

    for (int i = 0; i < kBufferSlots; ++i)
    {
        if (buffers_[i].Buffer() == buffer->Buffer())
            busy_[i] = true;
    }

    wl_surface_attach(surface_, buffer->Buffer(), 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, contentRect_.width, contentRect_.height);
    wl_surface_commit(surface_);
    session_.Flush();

    mapped_ = true;
    drawnRect_ = contentRect_;
    drawnTexts_ = state_.texts;
    drawnPreview_ = state_.previewEnabled;
    drawnFontSize_ = state_.fontSize;
}

void WaylandOverlayBackend::Render(const OverlayState& state)
{
    if (!running_ || !session_.LayerShell())
        return;

    state_ = state;

    int thickness = 1;
    double fontScale = 1.0;
    cv::Rect content = ComputeContentRect(fontScale, thickness);

    if (content.empty() || !visible_)
    {
        Hide();
        return;
    }

    const WaylandOutput* target = session_.OutputAt(content.x, content.y);

    if (!target)
        target = session_.PrimaryOutput();

    if (!target)
    {
        Hide();
        return;
    }

    content &= cv::Rect(target->x, target->y, target->LogicalWidth(), target->LogicalHeight());

    if (content.empty())
    {
        Hide();
        return;
    }

    if (!surface_ || output_ != target)
    {
        DestroySurface();

        if (!CreateSurface(target))
            return;
    }

    if (content != contentRect_)
    {
        contentRect_ = content;
        UpdateGeometry();
    }

    const bool unchanged =
        mapped_ &&
        drawnRect_ == contentRect_ &&
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
        drawnRect_ = cv::Rect();
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
