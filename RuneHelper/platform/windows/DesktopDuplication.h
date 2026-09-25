#pragma once

#include <opencv2/core.hpp>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class DesktopDuplication
{
public:
    cv::Mat CaptureRegion(const cv::Rect& region);
    void Shutdown();

private:
    struct FrameCopy
    {
        cv::Mat gray;
        bool resetDevice = false;
    };

    bool InitForRegion(const cv::Rect& region);
    bool CoversRegion(const cv::Rect& region) const;
    bool HasCachedFrame(const cv::Rect& region) const;
    FrameCopy CopyFrame(const cv::Rect& region, const Microsoft::WRL::ComPtr<IDXGIResource>& desktopResource);
    HRESULT EnsureStagingTexture(const cv::Size& size, DXGI_FORMAT format);

    bool initialized_ = false;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;

    int outputLeft_ = 0;
    int outputTop_ = 0;
    int outputRight_ = 0;
    int outputBottom_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture_;
    UINT stagingWidth_ = 0;
    UINT stagingHeight_ = 0;
    DXGI_FORMAT stagingFormat_ = DXGI_FORMAT_UNKNOWN;

    cv::Mat lastFrame_;
    cv::Rect lastFrameRegion_;

    bool loggedScaledDesktop_ = false;
};
