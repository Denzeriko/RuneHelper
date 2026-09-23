#pragma once

#include <memory>
#include <string>

#include <opencv2/core.hpp>

class PortalScreenCast
{
public:
    PortalScreenCast();
    ~PortalScreenCast();

    PortalScreenCast(const PortalScreenCast&) = delete;
    PortalScreenCast& operator=(const PortalScreenCast&) = delete;

    bool Start(std::string& restoreToken);
    void Stop();
    bool IsRunning() const;

    void Cancel();
    bool Cancelled() const;

    cv::Mat LatestFrame();
    cv::Point FramePosition() const;
    bool HasFramePosition() const;
    cv::Size FrameLogicalSize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
