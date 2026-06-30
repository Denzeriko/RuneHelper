#pragma once

#include <opencv2/opencv.hpp>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

class ScreenCaptureWGC
{
public:
    bool InitForRegion(const cv::Rect& region);
    cv::Mat CaptureRegion(const cv::Rect& region);
    void Shutdown();

private:
    bool initialized_ = false;

    int outputLeft_ = 0;
    int outputTop_ = 0;
    int outputRight_ = 0;
    int outputBottom_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture_;
    UINT stagingWidth_ = 0;
    UINT stagingHeight_ = 0;
    DXGI_FORMAT stagingFormat_ = DXGI_FORMAT_UNKNOWN;
};