#include "WaylandSession.h"

#include <poll.h>
#include <algorithm>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

#include "core/Logger.h"

const wl_registry_listener WaylandSession::kRegistryListener = {
    &WaylandSession::HandleGlobal,
    &WaylandSession::HandleGlobalRemove
};

const wl_output_listener WaylandSession::kOutputListener = {
    &WaylandSession::HandleOutputGeometry,
    &WaylandSession::HandleOutputMode,
    &WaylandSession::HandleOutputDone,
    &WaylandSession::HandleOutputScale,
    &WaylandSession::HandleOutputName,
    &WaylandSession::HandleOutputDescription
};

int WaylandOutput::LogicalWidth() const
{
    return scale > 0 ? width / scale : width;
}

int WaylandOutput::LogicalHeight() const
{
    return scale > 0 ? height / scale : height;
}

bool WaylandOutput::Contains(int px, int py) const
{
    return px >= x && py >= y && px < x + LogicalWidth() && py < y + LogicalHeight();
}

WaylandSession::~WaylandSession()
{
    Disconnect();
}

void WaylandSession::HandleGlobal(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version)
{
    auto* session = static_cast<WaylandSession*>(data);
    const std::string iface(interface);

    if (iface == wl_compositor_interface.name)
    {
        session->compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u))
        );
    }
    else if (iface == wl_shm_interface.name)
    {
        session->shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    }
    else if (iface == wl_seat_interface.name)
    {
        session->seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    }
    else if (iface == zwlr_layer_shell_v1_interface.name)
    {
        session->layerShell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u))
        );
    }
    else if (iface == zwlr_screencopy_manager_v1_interface.name)
    {
        session->screencopy_ = static_cast<zwlr_screencopy_manager_v1*>(
            wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, std::min(version, 3u))
        );
    }
    else if (iface == wl_output_interface.name)
    {
        WaylandOutput entry;
        entry.globalName = name;
        entry.output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u))
        );

        session->outputs_.push_back(entry);
        wl_output_add_listener(session->outputs_.back().output, &kOutputListener, session);
    }
}

void WaylandSession::HandleGlobalRemove(void* data, wl_registry*, std::uint32_t name)
{
    auto* session = static_cast<WaylandSession*>(data);

    auto it = std::find_if(
        session->outputs_.begin(),
        session->outputs_.end(),
        [name](const WaylandOutput& output) { return output.globalName == name; }
    );

    if (it == session->outputs_.end())
        return;

    if (it->output)
        wl_output_destroy(it->output);

    session->outputs_.erase(it);
}

WaylandOutput* WaylandSession::FindOutput(wl_output* output)
{
    auto it = std::find_if(
        outputs_.begin(),
        outputs_.end(),
        [output](const WaylandOutput& entry) { return entry.output == output; }
    );

    return it == outputs_.end() ? nullptr : &(*it);
}

void WaylandSession::HandleOutputGeometry(void* data, wl_output* output, std::int32_t x, std::int32_t y, std::int32_t, std::int32_t, std::int32_t, const char*, const char*, std::int32_t)
{
    if (WaylandOutput* entry = static_cast<WaylandSession*>(data)->FindOutput(output))
    {
        entry->x = x;
        entry->y = y;
    }
}

void WaylandSession::HandleOutputMode(void* data, wl_output* output, std::uint32_t flags, std::int32_t width, std::int32_t height, std::int32_t)
{
    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;

    if (WaylandOutput* entry = static_cast<WaylandSession*>(data)->FindOutput(output))
    {
        entry->width = width;
        entry->height = height;
    }
}

void WaylandSession::HandleOutputDone(void*, wl_output*)
{
}

void WaylandSession::HandleOutputScale(void* data, wl_output* output, std::int32_t factor)
{
    if (WaylandOutput* entry = static_cast<WaylandSession*>(data)->FindOutput(output))
        entry->scale = factor > 0 ? factor : 1;
}

void WaylandSession::HandleOutputName(void*, wl_output*, const char*)
{
}

void WaylandSession::HandleOutputDescription(void*, wl_output*, const char*)
{
}

bool WaylandSession::Connect()
{
    if (display_)
        return true;

    display_ = wl_display_connect(nullptr);

    if (!display_)
    {
        LOG_ERROR("Wayland: wl_display_connect failed");
        return false;
    }

    outputs_.reserve(8);

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);

    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!compositor_ || !shm_)
    {
        LOG_ERROR("Wayland: compositor did not advertise wl_compositor/wl_shm");
        Disconnect();
        return false;
    }

    return true;
}

void WaylandSession::Disconnect()
{
    for (WaylandOutput& output : outputs_)
    {
        if (output.output)
            wl_output_destroy(output.output);
    }

    outputs_.clear();

    if (screencopy_)
    {
        zwlr_screencopy_manager_v1_destroy(screencopy_);
        screencopy_ = nullptr;
    }

    if (layerShell_)
    {
        zwlr_layer_shell_v1_destroy(layerShell_);
        layerShell_ = nullptr;
    }

    if (seat_)
    {
        wl_seat_destroy(seat_);
        seat_ = nullptr;
    }

    if (shm_)
    {
        wl_shm_destroy(shm_);
        shm_ = nullptr;
    }

    if (compositor_)
    {
        wl_compositor_destroy(compositor_);
        compositor_ = nullptr;
    }

    if (registry_)
    {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }

    if (display_)
    {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

bool WaylandSession::IsConnected() const
{
    return display_ != nullptr;
}

bool WaylandSession::Roundtrip()
{
    return display_ && wl_display_roundtrip(display_) != -1;
}

bool WaylandSession::Dispatch()
{
    return display_ && wl_display_dispatch(display_) != -1;
}

bool WaylandSession::DispatchPending()
{
    return display_ && wl_display_dispatch_pending(display_) != -1;
}

bool WaylandSession::DispatchNonBlocking()
{
    if (!display_)
        return false;

    while (wl_display_prepare_read(display_) != 0)
    {
        if (wl_display_dispatch_pending(display_) == -1)
            return false;
    }

    wl_display_flush(display_);

    pollfd entry{};
    entry.fd = wl_display_get_fd(display_);
    entry.events = POLLIN;

    if (poll(&entry, 1, 0) > 0 && (entry.revents & POLLIN))
    {
        if (wl_display_read_events(display_) == -1)
            return false;
    }
    else
    {
        wl_display_cancel_read(display_);
    }

    return wl_display_dispatch_pending(display_) != -1;
}

void WaylandSession::Flush()
{
    if (display_)
        wl_display_flush(display_);
}

wl_display* WaylandSession::Display() const
{
    return display_;
}

wl_compositor* WaylandSession::Compositor() const
{
    return compositor_;
}

wl_shm* WaylandSession::Shm() const
{
    return shm_;
}

wl_seat* WaylandSession::Seat() const
{
    return seat_;
}

zwlr_layer_shell_v1* WaylandSession::LayerShell() const
{
    return layerShell_;
}

zwlr_screencopy_manager_v1* WaylandSession::Screencopy() const
{
    return screencopy_;
}

const std::vector<WaylandOutput>& WaylandSession::Outputs() const
{
    return outputs_;
}

const WaylandOutput* WaylandSession::OutputAt(int px, int py) const
{
    for (const WaylandOutput& output : outputs_)
    {
        if (output.Contains(px, py))
            return &output;
    }

    return nullptr;
}

const WaylandOutput* WaylandSession::OutputByName(std::uint32_t globalName) const
{
    for (const WaylandOutput& output : outputs_)
    {
        if (output.globalName == globalName)
            return &output;
    }

    return nullptr;
}

const WaylandOutput* WaylandSession::PrimaryOutput() const
{
    const WaylandOutput* best = nullptr;

    for (const WaylandOutput& output : outputs_)
    {
        if (!best || output.x < best->x || (output.x == best->x && output.y < best->y))
            best = &output;
    }

    return best;
}

WaylandShmBuffer::~WaylandShmBuffer()
{
    Destroy();
}

bool WaylandShmBuffer::Create(wl_shm* shm, int width, int height, int stride, std::uint32_t format)
{
    Destroy();

    if (!shm || width <= 0 || height <= 0 || stride <= 0)
        return false;

    const std::size_t size = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    const int fd = memfd_create("runehelper-shm", MFD_CLOEXEC);

    if (fd < 0)
    {
        LOG_ERROR("Wayland: memfd_create failed");
        return false;
    }

    if (ftruncate(fd, static_cast<off_t>(size)) != 0)
    {
        LOG_ERROR("Wayland: ftruncate failed for shm buffer");
        close(fd);
        return false;
    }

    void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (mapped == MAP_FAILED)
    {
        LOG_ERROR("Wayland: mmap failed for shm buffer");
        close(fd);
        return false;
    }

    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<std::int32_t>(size));
    buffer_ = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);

    if (!buffer_)
    {
        munmap(mapped, size);
        LOG_ERROR("Wayland: wl_shm_pool_create_buffer failed");
        return false;
    }

    data_ = static_cast<unsigned char*>(mapped);
    size_ = size;
    width_ = width;
    height_ = height;
    stride_ = stride;
    format_ = format;
    return true;
}

bool WaylandShmBuffer::Matches(int width, int height, int stride, std::uint32_t format) const
{
    return buffer_ != nullptr &&
           data_ != nullptr &&
           width_ == width &&
           height_ == height &&
           stride_ == stride &&
           format_ == format;
}

void WaylandShmBuffer::Destroy()
{
    if (buffer_)
    {
        wl_buffer_destroy(buffer_);
        buffer_ = nullptr;
    }

    if (data_)
    {
        munmap(data_, size_);
        data_ = nullptr;
    }

    size_ = 0;
    width_ = 0;
    height_ = 0;
    stride_ = 0;
    format_ = 0;
}

bool WaylandShmBuffer::IsValid() const
{
    return buffer_ != nullptr && data_ != nullptr;
}

wl_buffer* WaylandShmBuffer::Buffer() const
{
    return buffer_;
}

unsigned char* WaylandShmBuffer::Data() const
{
    return data_;
}

int WaylandShmBuffer::Width() const
{
    return width_;
}

int WaylandShmBuffer::Height() const
{
    return height_;
}

int WaylandShmBuffer::Stride() const
{
    return stride_;
}

std::uint32_t WaylandShmBuffer::Format() const
{
    return format_;
}
