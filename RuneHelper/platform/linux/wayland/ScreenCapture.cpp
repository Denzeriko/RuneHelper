#include "platform/linux/ScreenCapture.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include <opencv2/imgproc.hpp>

#include "PortalScreenCast.h"
#include "WaylandSession.h"
#include "core/Logger.h"
#include "platform/PlatformPaths.h"

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

std::string DescribeRect(const cv::Rect& rect)
{
    return std::to_string(rect.x) + "," + std::to_string(rect.y) + " " +
           std::to_string(rect.width) + "x" + std::to_string(rect.height);
}

std::string DescribeOutputs(const WaylandSession& session)
{
    if (session.Outputs().empty())
        return " (no outputs)";

    std::string text = " (outputs:";

    for (const WaylandOutput& output : session.Outputs())
    {
        text += " " + DescribeRect(cv::Rect(output.x, output.y, output.LogicalWidth(), output.LogicalHeight()));
    }

    return text + ")";
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

PortalScreenCast& Portal()
{
    static PortalScreenCast portal;
    return portal;
}

std::filesystem::path RestoreTokenPath()
{
    return GetAppDataDir() / "screencast_token";
}

std::string LoadRestoreToken()
{
    std::ifstream in(RestoreTokenPath());

    if (!in)
        return {};

    std::string token;
    std::getline(in, token);
    return token;
}

void SaveRestoreToken(const std::string& token)
{
    if (token.empty())
        return;

    std::ofstream out(RestoreTokenPath(), std::ios::trunc);

    if (out)
        out << token;
}

bool UsePortal()
{
    static const bool value = []
    {
        const char* forced = std::getenv("RUNEHELPER_CAPTURE_PORTAL");

        if (forced && forced[0] == '1')
            return true;

        WaylandSession& session = Session();
        return !session.Connect() || session.Screencopy() == nullptr;
    }();

    return value;
}

int& PortalMismatchCount()
{
    static int count = 0;
    return count;
}

cv::Mat CaptureViaPortal(const cv::Rect& region)
{
    PortalScreenCast& portal = Portal();

    if (!portal.IsRunning())
    {
        std::string token = LoadRestoreToken();

        if (!portal.Start(token))
            return {};

        SaveRestoreToken(token);
    }

    cv::Mat frame;

    for (int attempt = 0; attempt < 100 && frame.empty(); ++attempt)
    {
        frame = portal.LatestFrame();

        if (frame.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (frame.empty())
    {
        LOG_ERROR("Portal screencast: no frame arrived from the stream");
        return {};
    }

    cv::Point origin = portal.FramePosition();

    if (!portal.HasFramePosition())
    {
        if (const WaylandOutput* output = Session().OutputAt(region.x, region.y))
            origin = cv::Point(output->x, output->y);
    }

    const cv::Rect local(region.x - origin.x, region.y - origin.y, region.width, region.height);
    const cv::Rect clipped = local & cv::Rect(0, 0, frame.cols, frame.rows);

    if (clipped.empty())
    {
        int& mismatches = PortalMismatchCount();
        ++mismatches;

        if (mismatches == 1)
        {
            LOG_ERROR(
                "Portal screencast: the shared output covers " +
                std::to_string(origin.x) + "," + std::to_string(origin.y) + " " +
                std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
                " but the configured region is " +
                std::to_string(region.x) + "," + std::to_string(region.y) + " " +
                std::to_string(region.width) + "x" + std::to_string(region.height) +
                "; share the monitor that contains the region"
            );
        }

        if (mismatches >= 20)
        {
            LOG_ERROR("Portal screencast: dropping the saved permission so the screen picker opens again");
            portal.Stop();
            std::error_code ignored;
            std::filesystem::remove(RestoreTokenPath(), ignored);
            mismatches = 0;
        }

        return {};
    }

    PortalMismatchCount() = 0;
    return frame(clipped).clone();
}

cv::Mat Capture(const cv::Rect& region)
{
    if (UsePortal())
        return CaptureViaPortal(region);

    WaylandSession& session = Session();

    if (!session.Connect())
        return {};

    if (!session.DispatchNonBlocking())
    {
        LOG_ERROR("Wayland screen capture: display error, reconnecting");
        session.Disconnect();

        if (!session.Connect())
            return {};
    }

    if (!session.Screencopy())
    {
        LOG_ERROR("Wayland screen capture requires zwlr_screencopy_manager_v1, which this compositor does not support");
        return {};
    }

    const WaylandOutput* output = OutputForRegion(session, region);

    if (!output)
    {
        LOG_ERROR("Wayland screen capture failed: no outputs are known" + DescribeOutputs(session));
        return {};
    }

    const cv::Rect bounds(output->x, output->y, output->LogicalWidth(), output->LogicalHeight());
    const cv::Rect clipped = region & bounds;

    if (clipped.empty())
    {
        LOG_ERROR(
            "Wayland screen capture failed: region " + DescribeRect(region) +
            " is outside every output" + DescribeOutputs(session)
        );
        return {};
    }

    const cv::Rect local(clipped.x - output->x, clipped.y - output->y, clipped.width, clipped.height);
    cv::Mat result = CaptureOutputRegion(*output, local);

    if (!result.empty() && (result.cols != local.width || result.rows != local.height))
        cv::resize(result, result, cv::Size(local.width, local.height), 0, 0, cv::INTER_AREA);

    return result;
}
}

cv::Mat CaptureRegion(const cv::Rect& region)
{
    return Capture(region);
}
