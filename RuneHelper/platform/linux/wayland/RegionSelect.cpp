#include "platform/linux/RegionSelect.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <linux/input-event-codes.h>
#include <unistd.h>
#include <opencv2/imgproc.hpp>

#include "WaylandSession.h"
#include "core/Logger.h"

namespace
{
cv::Rect RectFromPoints(int x1, int y1, int x2, int y2)
{
    const int left = std::min(x1, x2);
    const int top = std::min(y1, y2);
    const int right = std::max(x1, x2);
    const int bottom = std::max(y1, y2);

    return cv::Rect(left, top, right - left, bottom - top);
}

class RegionSelectSession
{
public:
    ~RegionSelectSession();

    bool Start();
    cv::Rect Run();

private:
    struct Surface
    {
        const WaylandOutput* output = nullptr;
        wl_surface* surface = nullptr;
        zwlr_layer_surface_v1* layerSurface = nullptr;
        WaylandShmBuffer buffer;
        int width = 0;
        int height = 0;
        int scale = 1;
        bool configured = false;
    };

    static void HandleConfigure(void* data, zwlr_layer_surface_v1* layerSurface, std::uint32_t serial, std::uint32_t width, std::uint32_t height);
    static void HandleClosed(void* data, zwlr_layer_surface_v1* layerSurface);

    static void HandlePointerEnter(void* data, wl_pointer* pointer, std::uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y);
    static void HandlePointerLeave(void* data, wl_pointer* pointer, std::uint32_t serial, wl_surface* surface);
    static void HandlePointerMotion(void* data, wl_pointer* pointer, std::uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void HandlePointerButton(void* data, wl_pointer* pointer, std::uint32_t serial, std::uint32_t time, std::uint32_t button, std::uint32_t state);
    static void HandlePointerAxis(void* data, wl_pointer* pointer, std::uint32_t time, std::uint32_t axis, wl_fixed_t value);

    static void HandleKeyboardKeymap(void* data, wl_keyboard* keyboard, std::uint32_t format, std::int32_t fd, std::uint32_t size);
    static void HandleKeyboardEnter(void* data, wl_keyboard* keyboard, std::uint32_t serial, wl_surface* surface, wl_array* keys);
    static void HandleKeyboardLeave(void* data, wl_keyboard* keyboard, std::uint32_t serial, wl_surface* surface);
    static void HandleKeyboardKey(void* data, wl_keyboard* keyboard, std::uint32_t serial, std::uint32_t time, std::uint32_t key, std::uint32_t state);
    static void HandleKeyboardModifiers(void* data, wl_keyboard* keyboard, std::uint32_t serial, std::uint32_t depressed, std::uint32_t latched, std::uint32_t locked, std::uint32_t group);

    Surface* FindSurface(wl_surface* surface);
    Surface* ActiveSurface();
    void DrawSurface(Surface& target);
    void DrawAll();

    WaylandSession session_;
    std::vector<std::unique_ptr<Surface>> surfaces_;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    wl_surface* activeSurface_ = nullptr;

    int pointerX_ = 0;
    int pointerY_ = 0;
    int startX_ = 0;
    int startY_ = 0;
    bool dragging_ = false;
    bool finished_ = false;
    bool cancelled_ = false;
    cv::Rect selection_;

    static const zwlr_layer_surface_v1_listener kLayerSurfaceListener;
    static const wl_pointer_listener kPointerListener;
    static const wl_keyboard_listener kKeyboardListener;
};

const zwlr_layer_surface_v1_listener RegionSelectSession::kLayerSurfaceListener = {
    &RegionSelectSession::HandleConfigure,
    &RegionSelectSession::HandleClosed
};

const wl_pointer_listener RegionSelectSession::kPointerListener = []
{
    wl_pointer_listener listener{};
    listener.enter = &RegionSelectSession::HandlePointerEnter;
    listener.leave = &RegionSelectSession::HandlePointerLeave;
    listener.motion = &RegionSelectSession::HandlePointerMotion;
    listener.button = &RegionSelectSession::HandlePointerButton;
    listener.axis = &RegionSelectSession::HandlePointerAxis;
    return listener;
}();

const wl_keyboard_listener RegionSelectSession::kKeyboardListener = []
{
    wl_keyboard_listener listener{};
    listener.keymap = &RegionSelectSession::HandleKeyboardKeymap;
    listener.enter = &RegionSelectSession::HandleKeyboardEnter;
    listener.leave = &RegionSelectSession::HandleKeyboardLeave;
    listener.key = &RegionSelectSession::HandleKeyboardKey;
    listener.modifiers = &RegionSelectSession::HandleKeyboardModifiers;
    return listener;
}();

RegionSelectSession::~RegionSelectSession()
{
    activeSurface_ = nullptr;
    dragging_ = false;

    if (pointer_)
    {
        wl_pointer_destroy(pointer_);
        pointer_ = nullptr;
    }

    if (keyboard_)
    {
        wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
    }

    for (std::unique_ptr<Surface>& surface : surfaces_)
    {
        if (!surface->surface)
            continue;

        wl_surface_attach(surface->surface, nullptr, 0, 0);
        wl_surface_commit(surface->surface);
    }

    session_.Flush();

    for (std::unique_ptr<Surface>& surface : surfaces_)
    {
        surface->buffer.Destroy();

        if (surface->layerSurface)
            zwlr_layer_surface_v1_destroy(surface->layerSurface);

        if (surface->surface)
            wl_surface_destroy(surface->surface);
    }

    surfaces_.clear();
    session_.Flush();
    session_.Disconnect();
}

RegionSelectSession::Surface* RegionSelectSession::FindSurface(wl_surface* surface)
{
    for (std::unique_ptr<Surface>& entry : surfaces_)
    {
        if (entry->surface == surface)
            return entry.get();
    }

    return nullptr;
}

RegionSelectSession::Surface* RegionSelectSession::ActiveSurface()
{
    return activeSurface_ ? FindSurface(activeSurface_) : nullptr;
}

void RegionSelectSession::HandleConfigure(void* data, zwlr_layer_surface_v1* layerSurface, std::uint32_t serial, std::uint32_t width, std::uint32_t height)
{
    auto* session = static_cast<RegionSelectSession*>(data);
    zwlr_layer_surface_v1_ack_configure(layerSurface, serial);

    for (std::unique_ptr<Surface>& surface : session->surfaces_)
    {
        if (surface->layerSurface != layerSurface)
            continue;

        surface->width = width > 0 ? static_cast<int>(width) : surface->output->LogicalWidth();
        surface->height = height > 0 ? static_cast<int>(height) : surface->output->LogicalHeight();
        surface->configured = true;
        session->DrawSurface(*surface);
    }
}

void RegionSelectSession::HandleClosed(void* data, zwlr_layer_surface_v1*)
{
    auto* session = static_cast<RegionSelectSession*>(data);
    session->cancelled_ = true;
    session->finished_ = true;
}

void RegionSelectSession::HandlePointerEnter(void* data, wl_pointer*, std::uint32_t, wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    auto* session = static_cast<RegionSelectSession*>(data);
    Surface* target = session->FindSurface(surface);

    if (!target)
    {
        session->activeSurface_ = nullptr;
        return;
    }

    session->activeSurface_ = surface;
    session->pointerX_ = target->output->x + static_cast<int>(wl_fixed_to_double(x));
    session->pointerY_ = target->output->y + static_cast<int>(wl_fixed_to_double(y));
}

void RegionSelectSession::HandlePointerLeave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface)
{
    auto* session = static_cast<RegionSelectSession*>(data);

    if (session->activeSurface_ == surface)
        session->activeSurface_ = nullptr;
}

void RegionSelectSession::HandlePointerMotion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y)
{
    auto* session = static_cast<RegionSelectSession*>(data);
    Surface* target = session->ActiveSurface();

    if (!target)
        return;

    session->pointerX_ = target->output->x + static_cast<int>(wl_fixed_to_double(x));
    session->pointerY_ = target->output->y + static_cast<int>(wl_fixed_to_double(y));

    if (session->dragging_)
        session->DrawAll();
}

void RegionSelectSession::HandlePointerButton(void* data, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t button, std::uint32_t state)
{
    auto* session = static_cast<RegionSelectSession*>(data);

    if (button != BTN_LEFT || !session->ActiveSurface())
        return;

    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        session->dragging_ = true;
        session->startX_ = session->pointerX_;
        session->startY_ = session->pointerY_;
        return;
    }

    if (!session->dragging_)
        return;

    session->dragging_ = false;
    session->selection_ = RectFromPoints(session->startX_, session->startY_, session->pointerX_, session->pointerY_);
    session->finished_ = true;
}

void RegionSelectSession::HandlePointerAxis(void*, wl_pointer*, std::uint32_t, std::uint32_t, wl_fixed_t)
{
}

void RegionSelectSession::HandleKeyboardKeymap(void*, wl_keyboard*, std::uint32_t, std::int32_t fd, std::uint32_t)
{
    if (fd >= 0)
        close(fd);
}

void RegionSelectSession::HandleKeyboardEnter(void*, wl_keyboard*, std::uint32_t, wl_surface*, wl_array*)
{
}

void RegionSelectSession::HandleKeyboardLeave(void*, wl_keyboard*, std::uint32_t, wl_surface*)
{
}

void RegionSelectSession::HandleKeyboardKey(void* data, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key, std::uint32_t state)
{
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || key != KEY_ESC)
        return;

    auto* session = static_cast<RegionSelectSession*>(data);
    session->cancelled_ = true;
    session->finished_ = true;
}

void RegionSelectSession::HandleKeyboardModifiers(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)
{
}

void RegionSelectSession::DrawSurface(Surface& target)
{
    if (!target.configured || target.width <= 0 || target.height <= 0)
        return;

    const int bufferWidth = target.width * target.scale;
    const int bufferHeight = target.height * target.scale;

    if (!target.buffer.IsValid() || target.buffer.Width() != bufferWidth || target.buffer.Height() != bufferHeight)
    {
        if (!target.buffer.Create(session_.Shm(), bufferWidth, bufferHeight, bufferWidth * 4, WL_SHM_FORMAT_ARGB8888))
            return;
    }

    cv::Mat canvas(bufferHeight, bufferWidth, CV_8UC4, target.buffer.Data(), static_cast<std::size_t>(target.buffer.Stride()));
    canvas.setTo(cv::Scalar(0, 0, 0, 64));

    if (dragging_)
    {
        const cv::Rect global = RectFromPoints(startX_, startY_, pointerX_, pointerY_);
        const cv::Rect local(
            (global.x - target.output->x) * target.scale,
            (global.y - target.output->y) * target.scale,
            global.width * target.scale,
            global.height * target.scale
        );

        const cv::Rect clipped = local & cv::Rect(0, 0, bufferWidth, bufferHeight);

        if (!clipped.empty())
        {
            canvas(clipped).setTo(cv::Scalar(0, 0, 0, 0));
            cv::rectangle(canvas, clipped, cv::Scalar(255, 255, 255, 255), 2);
        }
    }

    wl_surface_set_buffer_scale(target.surface, target.scale);
    wl_surface_attach(target.surface, target.buffer.Buffer(), 0, 0);
    wl_surface_damage_buffer(target.surface, 0, 0, bufferWidth, bufferHeight);
    wl_surface_commit(target.surface);
}

void RegionSelectSession::DrawAll()
{
    for (std::unique_ptr<Surface>& surface : surfaces_)
        DrawSurface(*surface);

    session_.Flush();
}

bool RegionSelectSession::Start()
{
    if (!session_.Connect())
        return false;

    if (!session_.LayerShell())
    {
        LOG_ERROR("Wayland region selection requires zwlr_layer_shell_v1, which this compositor does not support");
        return false;
    }

    if (!session_.Seat())
    {
        LOG_ERROR("Wayland region selection requires a wl_seat");
        return false;
    }

    pointer_ = wl_seat_get_pointer(session_.Seat());
    keyboard_ = wl_seat_get_keyboard(session_.Seat());

    if (pointer_)
        wl_pointer_add_listener(pointer_, &kPointerListener, this);

    if (keyboard_)
        wl_keyboard_add_listener(keyboard_, &kKeyboardListener, this);

    for (const WaylandOutput& output : session_.Outputs())
    {
        auto surface = std::make_unique<Surface>();
        surface->output = &output;
        surface->scale = std::max(1, output.scale);
        surface->surface = wl_compositor_create_surface(session_.Compositor());

        if (!surface->surface)
            continue;

        surface->layerSurface = zwlr_layer_shell_v1_get_layer_surface(
            session_.LayerShell(),
            surface->surface,
            output.output,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
            "runehelper-region"
        );

        if (!surface->layerSurface)
        {
            wl_surface_destroy(surface->surface);
            continue;
        }

        zwlr_layer_surface_v1_add_listener(surface->layerSurface, &kLayerSurfaceListener, this);
        zwlr_layer_surface_v1_set_anchor(
            surface->layerSurface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
        );
        zwlr_layer_surface_v1_set_exclusive_zone(surface->layerSurface, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(surface->layerSurface, 1);

        wl_surface_commit(surface->surface);
        surfaces_.push_back(std::move(surface));
    }

    if (surfaces_.empty())
    {
        LOG_ERROR("Wayland region selection failed: no output surfaces could be created");
        return false;
    }

    session_.Roundtrip();
    return true;
}

cv::Rect RegionSelectSession::Run()
{
    LOG_INFO("Wayland region selection started");

    while (!finished_)
    {
        if (!session_.Dispatch())
        {
            LOG_ERROR("Wayland region selection failed: display dispatch error");
            return {};
        }
    }

    if (cancelled_)
    {
        LOG_INFO("Wayland region selection cancelled");
        return {};
    }

    return selection_;
}
}

RegionSelector::~RegionSelector() = default;

cv::Rect RegionSelector::Select()
{
    RegionSelectSession session;

    if (!session.Start())
        return {};

    const cv::Rect selected = session.Run();

    if (selected.width < 2 || selected.height < 2)
    {
        if (!selected.empty())
            LOG_ERROR("Wayland region selection ignored: selected rectangle is too small");

        return {};
    }

    LOG_INFO(
        "Wayland region selected: x=" + std::to_string(selected.x) +
        " y=" + std::to_string(selected.y) +
        " w=" + std::to_string(selected.width) +
        " h=" + std::to_string(selected.height)
    );

    return selected;
}
