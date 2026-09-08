#include "platform/linux/ScreenCapture.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "WaylandSession.h"
#include "core/Logger.h"

namespace
{
struct CaptureFrame
{
    WaylandSession* session = nullptr;
    zwlr_screencopy_frame_v1* frame = nullptr;
    WaylandShmBuffer buffer;
    bool copyRequested = false;
    bool done = false;
    bool failed = false;
    bool yInvert = false;
};

void RequestCopy(CaptureFrame* capture)
{
    if (capture->copyRequested || capture->failed || !capture->buffer.IsValid())
        return;

    capture->copyRequested = true;
    zwlr_screencopy_frame_v1_copy(capture->frame, capture->buffer.Buffer());
}

void HandleBuffer(void* data, zwlr_screencopy_frame_v1* frame, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t stride)
{
    auto* capture = static_cast<CaptureFrame*>(data);

    if (!capture->buffer.Create(capture->session->Shm(), static_cast<int>(width), static_cast<int>(height), static_cast<int>(stride), format))
    {
        capture->failed = true;
        return;
    }

    if (zwlr_screencopy_frame_v1_get_version(frame) < 3)
        RequestCopy(capture);
}

void HandleFlags(void* data, zwlr_screencopy_frame_v1*, std::uint32_t flags)
{
    auto* capture = static_cast<CaptureFrame*>(data);
    capture->yInvert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

void HandleReady(void* data, zwlr_screencopy_frame_v1*, std::uint32_t, std::uint32_t, std::uint32_t)
{
    static_cast<CaptureFrame*>(data)->done = true;
}

void HandleFailed(void* data, zwlr_screencopy_frame_v1*)
{
    auto* capture = static_cast<CaptureFrame*>(data);
    capture->failed = true;
    capture->done = true;
}

void HandleDamage(void*, zwlr_screencopy_frame_v1*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)
{
}

void HandleLinuxDmabuf(void*, zwlr_screencopy_frame_v1*, std::uint32_t, std::uint32_t, std::uint32_t)
{
}

void HandleBufferDone(void* data, zwlr_screencopy_frame_v1*)
{
    RequestCopy(static_cast<CaptureFrame*>(data));
}

const zwlr_screencopy_frame_v1_listener kFrameListener = {
    &HandleBuffer,
    &HandleFlags,
    &HandleReady,
    &HandleFailed,
    &HandleDamage,
    &HandleLinuxDmabuf,
    &HandleBufferDone
};

cv::Mat ToBgr(const WaylandShmBuffer& buffer, bool yInvert)
{
    cv::Mat wrapped(buffer.Height(), buffer.Width(), CV_8UC4, buffer.Data(), static_cast<std::size_t>(buffer.Stride()));
    cv::Mat result;

    switch (buffer.Format())
    {
    case WL_SHM_FORMAT_XRGB8888:
    case WL_SHM_FORMAT_ARGB8888:
        cv::cvtColor(wrapped, result, cv::COLOR_BGRA2BGR);
        break;
    case WL_SHM_FORMAT_XBGR8888:
    case WL_SHM_FORMAT_ABGR8888:
        cv::cvtColor(wrapped, result, cv::COLOR_RGBA2BGR);
        break;
    default:
        LOG_ERROR("Wayland screen capture failed: unsupported shm format " + std::to_string(buffer.Format()));
        return {};
    }

    if (yInvert)
        cv::flip(result, result, 0);

    return result;
}

WaylandSession& Session()
{
    static WaylandSession session;
    return session;
}

const WaylandOutput* OutputForRegion(const WaylandSession& session, const cv::Rect& region)
{
    if (const WaylandOutput* output = session.OutputAt(region.x, region.y))
        return output;

    const WaylandOutput* best = nullptr;
    int bestArea = 0;

    for (const WaylandOutput& output : session.Outputs())
    {
        const cv::Rect bounds(output.x, output.y, output.LogicalWidth(), output.LogicalHeight());
        const int area = (bounds & region).area();

        if (area > bestArea)
        {
            bestArea = area;
            best = &output;
        }
    }

    return best ? best : session.PrimaryOutput();
}

cv::Mat CaptureOutputRegion(const WaylandOutput& output, const cv::Rect& local)
{
    WaylandSession& session = Session();

    CaptureFrame capture;
    capture.session = &session;
    capture.frame = zwlr_screencopy_manager_v1_capture_output_region(
        session.Screencopy(),
        0,
        output.output,
        local.x,
        local.y,
        local.width,
        local.height
    );

    if (!capture.frame)
    {
        LOG_ERROR("Wayland screen capture failed: capture_output_region returned null");
        return {};
    }

    zwlr_screencopy_frame_v1_add_listener(capture.frame, &kFrameListener, &capture);

    while (!capture.done && !capture.failed)
    {
        if (!session.Dispatch())
        {
            LOG_ERROR("Wayland screen capture failed: display dispatch error");
            session.Disconnect();
            return {};
        }
    }

    cv::Mat result;

    if (!capture.failed && capture.buffer.IsValid())
        result = ToBgr(capture.buffer, capture.yInvert);
    else
        LOG_ERROR("Wayland screen capture failed: compositor rejected the frame");

    zwlr_screencopy_frame_v1_destroy(capture.frame);
    return result;
}

cv::Mat Capture(const cv::Rect& region)
{
    WaylandSession& session = Session();

    if (!session.Connect())
        return {};

    if (!session.Screencopy())
    {
        LOG_ERROR("Wayland screen capture requires zwlr_screencopy_manager_v1, which this compositor does not support");
        return {};
    }

    const WaylandOutput* output = OutputForRegion(session, region);

    if (!output)
    {
        LOG_ERROR("Wayland screen capture failed: no output covers the requested region");
        return {};
    }

    const cv::Rect bounds(output->x, output->y, output->LogicalWidth(), output->LogicalHeight());
    const cv::Rect clipped = region & bounds;

    if (clipped.empty())
    {
        LOG_ERROR("Wayland screen capture failed: requested region is outside every output");
        return {};
    }

    const cv::Rect local(clipped.x - output->x, clipped.y - output->y, clipped.width, clipped.height);
    cv::Mat result = CaptureOutputRegion(*output, local);

    if (!result.empty() && (result.cols != local.width || result.rows != local.height))
        cv::resize(result, result, cv::Size(local.width, local.height), 0, 0, cv::INTER_AREA);

    return result;
}
}

cv::Mat CaptureScreen()
{
    WaylandSession& session = Session();

    if (!session.Connect())
        return {};

    const WaylandOutput* output = session.PrimaryOutput();

    if (!output)
    {
        LOG_ERROR("Wayland screen capture failed: compositor advertised no outputs");
        return {};
    }

    return Capture(cv::Rect(output->x, output->y, output->LogicalWidth(), output->LogicalHeight()));
}

cv::Mat CaptureRegion(const cv::Rect& region)
{
    return Capture(region);
}
