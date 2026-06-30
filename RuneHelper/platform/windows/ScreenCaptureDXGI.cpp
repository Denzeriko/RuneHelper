#include "ScreenCaptureDXGI.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dxgi1_2.h>

#include "core/Logger.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    ComPtr<ID3D11Device> g_device;
    ComPtr<ID3D11DeviceContext> g_context;
    ComPtr<IDXGIOutputDuplication> g_duplication;

    bool IntersectsOutput(const cv::Rect& region, const RECT& rc)
    {
        const int left = region.x;
        const int top = region.y;
        const int right = region.x + region.width;
        const int bottom = region.y + region.height;

        return left < rc.right && right > rc.left && top < rc.bottom && bottom > rc.top;
    }

    bool IsRecoverableDxgiError(HRESULT hr)
    {
        return hr == DXGI_ERROR_DEVICE_REMOVED ||
            hr == DXGI_ERROR_DEVICE_RESET ||
            hr == DXGI_ERROR_ACCESS_LOST ||
            hr == DXGI_ERROR_INVALID_CALL;
    }
}

bool ScreenCaptureWGC::InitForRegion(const cv::Rect& region)
{
    Shutdown();

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf()));

    if (FAILED(hr))
    {
        LOG_ERROR("CreateDXGIFactory1 failed");
        return false;
    }

    ComPtr<IDXGIAdapter1> selectedAdapter;
    ComPtr<IDXGIOutput> selectedOutput;

    LOG_INFO(
        "InitForRegion region: x=" + std::to_string(region.x) +
        " y=" + std::to_string(region.y) +
        " w=" + std::to_string(region.width) +
        " h=" + std::to_string(region.height));

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(adapterIndex, &adapter);

        if (hr == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(hr))
            continue;

        DXGI_ADAPTER_DESC1 adapterDesc{};
        adapter->GetDesc1(&adapterDesc);

        if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        for (UINT outputIndex = 0;; ++outputIndex)
        {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);

            if (hr == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(hr))
                continue;

            DXGI_OUTPUT_DESC outputDesc{};
            output->GetDesc(&outputDesc);

            RECT rc = outputDesc.DesktopCoordinates;

            LOG_INFO(
                "Output: left=" + std::to_string(rc.left) +
                " top=" + std::to_string(rc.top) +
                " right=" + std::to_string(rc.right) +
                " bottom=" + std::to_string(rc.bottom));

            if (!IntersectsOutput(region, rc))
                continue;

            selectedAdapter = adapter;
            selectedOutput = output;

            outputLeft_ = rc.left;
            outputTop_ = rc.top;
            outputRight_ = rc.right;
            outputBottom_ = rc.bottom;

            break;
        }

        if (selectedAdapter && selectedOutput)
            break;
    }

    if (!selectedAdapter || !selectedOutput)
    {
        LOG_ERROR("No matching output found for region");
        return false;
    }

    D3D_FEATURE_LEVEL featureLevel{};

    hr = D3D11CreateDevice(
        selectedAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &g_device,
        &featureLevel,
        &g_context);

    if (FAILED(hr))
    {
        LOG_ERROR("D3D11CreateDevice failed");
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    hr = selectedOutput.As(&output1);

    if (FAILED(hr))
    {
        LOG_ERROR("IDXGIOutput1 query failed");
        return false;
    }

    hr = output1->DuplicateOutput(g_device.Get(), &g_duplication);

    if (FAILED(hr))
    {
        char buf[128];
        sprintf_s(buf, "DuplicateOutput failed: 0x%08X", static_cast<unsigned>(hr));
        LOG_ERROR(buf);
        return false;
    }

    initialized_ = true;

    LOG_INFO("Desktop Duplication initialized");

    return true;
}

void ScreenCaptureWGC::Shutdown()
{
    stagingTexture_.Reset();
    stagingWidth_ = 0;
    stagingHeight_ = 0;
    stagingFormat_ = DXGI_FORMAT_UNKNOWN;

    g_duplication.Reset();
    g_context.Reset();
    g_device.Reset();

    initialized_ = false;

    outputLeft_ = 0;
    outputTop_ = 0;
    outputRight_ = 0;
    outputBottom_ = 0;
}

cv::Mat ScreenCaptureWGC::CaptureRegion(const cv::Rect& region)
{
    if (region.width <= 0 || region.height <= 0)
        return {};

    bool regionInsideCurrentOutput =
        initialized_ &&
        region.x >= outputLeft_ &&
        region.y >= outputTop_ &&
        region.x + region.width <= outputRight_ &&
        region.y + region.height <= outputBottom_;

    if (!regionInsideCurrentOutput)
    {
        Shutdown();

        if (!InitForRegion(region))
            return {};
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = g_duplication->AcquireNextFrame(16, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        return {};

    if (FAILED(hr))
    {
        char buf[128];
        sprintf_s(buf, "AcquireNextFrame failed: 0x%08X", static_cast<unsigned>(hr));
        LOG_ERROR(buf);

        if (IsRecoverableDxgiError(hr))
            Shutdown();

        return {};
    }

    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);

    if (FAILED(hr))
    {
        g_duplication->ReleaseFrame();
        return {};
    }

    D3D11_TEXTURE2D_DESC desktopDesc{};
    desktopTexture->GetDesc(&desktopDesc);

    int localX = region.x - outputLeft_;
    int localY = region.y - outputTop_;

    int outputWidth = outputRight_ - outputLeft_;
    int outputHeight = outputBottom_ - outputTop_;

    if (localX < 0 || localY < 0 || localX + region.width > outputWidth || localY + region.height > outputHeight)
    {
        LOG_ERROR(
            "Capture region is outside selected output: "
            "region x=" + std::to_string(region.x) +
            " y=" + std::to_string(region.y) +
            " w=" + std::to_string(region.width) +
            " h=" + std::to_string(region.height) +
            " output left=" + std::to_string(outputLeft_) +
            " top=" + std::to_string(outputTop_) +
            " right=" + std::to_string(outputRight_) +
            " bottom=" + std::to_string(outputBottom_) +
            " localX=" + std::to_string(localX) +
            " localY=" + std::to_string(localY));

        g_duplication->ReleaseFrame();
        Shutdown();

        return {};
    }

    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = static_cast<UINT>(region.width);
    stagingDesc.Height = static_cast<UINT>(region.height);
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = desktopDesc.Format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    if (!stagingTexture_ || stagingWidth_ != stagingDesc.Width || stagingHeight_ != stagingDesc.Height || stagingFormat_ != stagingDesc.Format)
    {
        stagingTexture_.Reset();

        hr = g_device->CreateTexture2D(
            &stagingDesc,
            nullptr,
            &stagingTexture_);

        if (FAILED(hr))
        {
            char buf[256];
            sprintf_s(
                buf,
                "CreateTexture2D failed: hr=0x%08X w=%u h=%u format=%u",
                static_cast<unsigned>(hr),
                stagingDesc.Width,
                stagingDesc.Height,
                stagingDesc.Format);

            LOG_ERROR(buf);

            if (g_device)
            {
                HRESULT reason = g_device->GetDeviceRemovedReason();

                char reasonBuf[128];
                sprintf_s(reasonBuf, "Device removed reason: 0x%08X", static_cast<unsigned>(reason));

                LOG_ERROR(reasonBuf);
            }

            g_duplication->ReleaseFrame();

            if (IsRecoverableDxgiError(hr))
                Shutdown();

            return {};
        }

        stagingWidth_ = stagingDesc.Width;
        stagingHeight_ = stagingDesc.Height;
        stagingFormat_ = stagingDesc.Format;
    }

    D3D11_BOX srcBox{};
    srcBox.left = static_cast<UINT>(localX);
    srcBox.top = static_cast<UINT>(localY);
    srcBox.right = static_cast<UINT>(localX + region.width);
    srcBox.bottom = static_cast<UINT>(localY + region.height);
    srcBox.front = 0;
    srcBox.back = 1;

    g_context->CopySubresourceRegion(stagingTexture_.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &srcBox);

    D3D11_MAPPED_SUBRESOURCE mapped{};

    hr = g_context->Map(
        stagingTexture_.Get(),
        0,
        D3D11_MAP_READ,
        0,
        &mapped);

    if (FAILED(hr))
    {
        char buf[128];
        sprintf_s(buf, "Map failed: 0x%08X", static_cast<unsigned>(hr));
        LOG_ERROR(buf);

        g_duplication->ReleaseFrame();

        if (IsRecoverableDxgiError(hr))
            Shutdown();

        return {};
    }

    cv::Mat bgra(region.height, region.width, CV_8UC4, mapped.pData, mapped.RowPitch);

    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

    cv::Mat result = bgr.clone();

    g_context->Unmap(stagingTexture_.Get(), 0);
    g_duplication->ReleaseFrame();

    return result;
}