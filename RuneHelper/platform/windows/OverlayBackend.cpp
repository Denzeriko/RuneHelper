#include "platform/OverlayBackend.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>

#include <opencv2/core.hpp>

#include "core/Logger.h"
#include "ui/OverlayRenderer.h"
#include "ui/OverlayState.h"

namespace
{
#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

class WindowsOverlayBackend final : public OverlayBackend
{
public:
    bool Init(const char* title, int width, int height) override;
    void Shutdown() override;

    bool IsRunning() const override;
    void PumpEvents() override;
    void Render(const OverlayState& state) override;

    void SetVisible(bool visible) override;
    void SetClickThrough(bool enabled) override;
    void SetAlwaysOnTop(bool enabled) override;
    void BringToTop() override;

private:
    bool EnsureSurface(int width, int height);
    void ReleaseSurface();
    void ApplyClickThrough(bool enabled);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    WNDCLASSW windowClass_ = {};

    HDC memoryDC_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* bits_ = nullptr;
    int surfaceW_ = 0;
    int surfaceH_ = 0;

    cv::Mat canvas_;
    OverlayState state_;

    bool running_ = false;
    bool visible_ = true;
    bool mapped_ = false;
};

bool WindowsOverlayBackend::Init(const char*, int, int)
{
    windowClass_ = {};
    windowClass_.lpfnWndProc = WndProc;
    windowClass_.hInstance = GetModuleHandleW(nullptr);
    windowClass_.lpszClassName = L"RuneHelperOverlay";

    RegisterClassW(&windowClass_);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        windowClass_.lpszClassName,
        L"RuneHelperOverlay",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        windowClass_.hInstance,
        this
    );

    if (!hwnd_)
    {
        LOG_ERROR("Windows overlay: CreateWindowEx failed");
        return false;
    }

    if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE))
        LOG_ERROR("Windows overlay: SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed");

    running_ = true;
    LOG_INFO("Windows overlay backend initialized");
    return true;
}

void WindowsOverlayBackend::Shutdown()
{
    running_ = false;

    ReleaseSurface();

    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    if (windowClass_.lpszClassName)
    {
        UnregisterClassW(windowClass_.lpszClassName, windowClass_.hInstance);
        windowClass_ = {};
    }
}

bool WindowsOverlayBackend::IsRunning() const
{
    return running_;
}

void WindowsOverlayBackend::PumpEvents()
{
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void WindowsOverlayBackend::ReleaseSurface()
{
    canvas_.release();

    if (memoryDC_)
    {
        if (oldBitmap_)
            SelectObject(memoryDC_, oldBitmap_);

        DeleteDC(memoryDC_);
        memoryDC_ = nullptr;
        oldBitmap_ = nullptr;
    }

    if (bitmap_)
    {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    bits_ = nullptr;
    surfaceW_ = 0;
    surfaceH_ = 0;
}

bool WindowsOverlayBackend::EnsureSurface(int width, int height)
{
    if (memoryDC_ && bitmap_ && surfaceW_ == width && surfaceH_ == height)
        return true;

    ReleaseSurface();

    HDC screenDC = GetDC(nullptr);

    if (!screenDC)
        return false;

    memoryDC_ = CreateCompatibleDC(screenDC);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bitmap_ = CreateDIBSection(screenDC, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);

    ReleaseDC(nullptr, screenDC);

    if (!memoryDC_ || !bitmap_ || !bits_)
    {
        LOG_ERROR("Windows overlay: could not create the layered surface");
        ReleaseSurface();
        return false;
    }

    oldBitmap_ = SelectObject(memoryDC_, bitmap_);

    canvas_ = cv::Mat(height, width, CV_8UC4, bits_, static_cast<std::size_t>(width) * 4);

    surfaceW_ = width;
    surfaceH_ = height;

    return true;
}

void WindowsOverlayBackend::Render(const OverlayState& state)
{
    if (!hwnd_ || !running_)
        return;

    state_ = state;

    const cv::Rect content = OverlayRenderer::ContentBounds(state_);

    if (content.empty() || !visible_)
    {
        if (mapped_)
        {
            ShowWindow(hwnd_, SW_HIDE);
            mapped_ = false;
        }

        return;
    }

    if (!EnsureSurface(content.width, content.height))
        return;

    canvas_.setTo(cv::Scalar(0, 0, 0, 0));
    OverlayRenderer::Paint(canvas_, content.tl(), state_);

    GdiFlush();

    POINT destination{ content.x, content.y };
    POINT source{ 0, 0 };
    SIZE size{ content.width, content.height };

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screenDC = GetDC(nullptr);

    const BOOL updated = UpdateLayeredWindow(
        hwnd_,
        screenDC,
        &destination,
        &size,
        memoryDC_,
        &source,
        0,
        &blend,
        ULW_ALPHA
    );

    ReleaseDC(nullptr, screenDC);

    if (!updated)
        return;

    if (!mapped_)
    {
        ShowWindow(hwnd_, SW_SHOWNA);
        mapped_ = true;
    }
}

void WindowsOverlayBackend::SetVisible(bool visible)
{
    visible_ = visible;

    if (!hwnd_ || visible_)
        return;

    ShowWindow(hwnd_, SW_HIDE);
    mapped_ = false;
}

void WindowsOverlayBackend::SetClickThrough(bool enabled)
{
    ApplyClickThrough(enabled);
}

void WindowsOverlayBackend::SetAlwaysOnTop(bool enabled)
{
    if (!hwnd_)
        return;

    SetWindowPos(
        hwnd_,
        enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
    );
}

void WindowsOverlayBackend::BringToTop()
{
    SetAlwaysOnTop(true);
}

void WindowsOverlayBackend::ApplyClickThrough(bool enabled)
{
    if (!hwnd_)
        return;

    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    style |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    if (enabled)
        style |= WS_EX_TRANSPARENT;
    else
        style &= ~WS_EX_TRANSPARENT;

    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
}

LRESULT CALLBACK WindowsOverlayBackend::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WindowsOverlayBackend* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<WindowsOverlayBackend*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    else
    {
        self = reinterpret_cast<WindowsOverlayBackend*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        self->running_ = false;
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
}

std::unique_ptr<OverlayBackend> CreateOverlayBackend()
{
    return std::make_unique<WindowsOverlayBackend>();
}
