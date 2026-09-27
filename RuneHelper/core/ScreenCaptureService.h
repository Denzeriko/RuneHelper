#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <stop_token>

#ifdef _WIN32
#include "platform/windows/DesktopDuplication.h"
#endif

class IScreenCapture
{
public:
    virtual ~IScreenCapture() = default;
    virtual cv::Mat CaptureRegion(const cv::Rect& region, const std::stop_token& stop) = 0;
    virtual void Cancel() = 0;
    virtual void Shutdown() = 0;
};

class ScreenCaptureService final : public IScreenCapture
{
public:
    cv::Mat CaptureRegion(const cv::Rect& region, const std::stop_token& stop) override;
    void Cancel() override;
    void Shutdown() override;

private:
#ifdef _WIN32
    DesktopDuplication desktopDuplication_;
#endif
};

std::unique_ptr<IScreenCapture> CreateScreenCapture();
