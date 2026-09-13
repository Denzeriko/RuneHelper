#pragma once

#include <cstdint>
#include <vector>

#include <wayland-client.h>

#define namespace layer_shell_namespace
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include "wlr-screencopy-unstable-v1-client-protocol.h"

struct WaylandOutput
{
    wl_output* output = nullptr;
    std::uint32_t globalName = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int scale = 1;

    int LogicalWidth() const;
    int LogicalHeight() const;
    bool Contains(int px, int py) const;
};

class WaylandSession
{
public:
    WaylandSession() = default;
    ~WaylandSession();

    WaylandSession(const WaylandSession&) = delete;
    WaylandSession& operator=(const WaylandSession&) = delete;

    bool Connect();
    void Disconnect();
    bool IsConnected() const;

    bool Roundtrip();
    bool Dispatch();
    bool DispatchPending();
    bool DispatchNonBlocking();
    void Flush();

    wl_display* Display() const;
    wl_compositor* Compositor() const;
    wl_shm* Shm() const;
    wl_seat* Seat() const;
    zwlr_layer_shell_v1* LayerShell() const;
    zwlr_screencopy_manager_v1* Screencopy() const;

    const std::vector<WaylandOutput>& Outputs() const;
    const WaylandOutput* OutputAt(int px, int py) const;
    const WaylandOutput* OutputByName(std::uint32_t globalName) const;
    const WaylandOutput* PrimaryOutput() const;

private:
    static void HandleGlobal(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version);
    static void HandleGlobalRemove(void* data, wl_registry* registry, std::uint32_t name);

    static void HandleOutputGeometry(void* data, wl_output* output, std::int32_t x, std::int32_t y, std::int32_t physicalWidth, std::int32_t physicalHeight, std::int32_t subpixel, const char* make, const char* model, std::int32_t transform);
    static void HandleOutputMode(void* data, wl_output* output, std::uint32_t flags, std::int32_t width, std::int32_t height, std::int32_t refresh);
    static void HandleOutputDone(void* data, wl_output* output);
    static void HandleOutputScale(void* data, wl_output* output, std::int32_t factor);
    static void HandleOutputName(void* data, wl_output* output, const char* name);
    static void HandleOutputDescription(void* data, wl_output* output, const char* description);

    WaylandOutput* FindOutput(wl_output* output);

    static const wl_registry_listener kRegistryListener;
    static const wl_output_listener kOutputListener;

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_seat* seat_ = nullptr;
    zwlr_layer_shell_v1* layerShell_ = nullptr;
    zwlr_screencopy_manager_v1* screencopy_ = nullptr;
    std::vector<WaylandOutput> outputs_;
};

class WaylandShmBuffer
{
public:
    WaylandShmBuffer() = default;
    ~WaylandShmBuffer();

    WaylandShmBuffer(const WaylandShmBuffer&) = delete;
    WaylandShmBuffer& operator=(const WaylandShmBuffer&) = delete;

    bool Create(wl_shm* shm, int width, int height, int stride, std::uint32_t format);
    bool Matches(int width, int height, int stride, std::uint32_t format) const;
    void Destroy();

    bool IsValid() const;
    wl_buffer* Buffer() const;
    unsigned char* Data() const;
    int Width() const;
    int Height() const;
    int Stride() const;
    std::uint32_t Format() const;

private:
    wl_buffer* buffer_ = nullptr;
    unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
    std::uint32_t format_ = 0;
};
