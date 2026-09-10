#pragma once

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

    cv::Mat LatestFrame();
    cv::Point FramePosition() const;
    bool HasFramePosition() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
