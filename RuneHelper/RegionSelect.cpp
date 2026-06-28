#include "RegionSelect.h"

#include "Logger.h"

#include <windowsx.h>
#include <algorithm>

RegionSelector::~RegionSelector()
{
    DestroyOverlayWindow();
}

cv::Rect RegionSelector::Select()
{
    LOG_INFO("RegionSelector::Select() -> call");

    done_ = false;
    cancelled_ = false;
    dragging_ = false;
    result_ = {};

    if (!CreateOverlayWindow())
    {
        LOG_ERROR("RegionSelector::Select() -> CreateOverlayWindow error!");
        return {};
    }

    MSG msg{};
    while (!done_ && GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyOverlayWindow();

    if (cancelled_)
    {
        LOG_ERROR("RegionSelector::Select() -> cancelled");
        return {};
    }
        

    LOG_INFO("RegionSelector::Select() -> return");

    return GetSelectedRect();
}

bool RegionSelector::CreateOverlayWindow()
{
    LOG_INFO("RegionSelector::CreateOverlayWindow() -> call");

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = RegionSelector::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"OCR_REGION_SELECTOR";
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);

    RegisterClassW(&wc);

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        wc.lpszClassName,
        L"Select region",
        WS_POPUP,
        x,
        y,
        w,
        h,
        nullptr,
        nullptr,
        hInst,
        this
    );

    if (!hwnd_) {
        LOG_ERROR("RegionSelector::CreateOverlayWindow() -> no HWND!");
        return false;
    }

    SetLayeredWindowAttributes(hwnd_, 0, 120, LWA_ALPHA);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    LOG_INFO("RegionSelector::CreateOverlayWindow() -> return");

    return true;
}

void RegionSelector::DestroyOverlayWindow()
{
    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

cv::Rect RegionSelector::GetSelectedRect() const
{
    int left = std::min(result_.left, result_.right);
    int top = std::min(result_.top, result_.bottom);
    int right = std::max(result_.left, result_.right);
    int bottom = std::max(result_.top, result_.bottom);

    return cv::Rect(
        left,
        top,
        right - left,
        bottom - top
    );
}

void RegionSelector::OnLeftButtonDown(LPARAM lp)
{
    dragging_ = true;

    start_.x = GET_X_LPARAM(lp);
    start_.y = GET_Y_LPARAM(lp);
    current_ = start_;

    SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void RegionSelector::OnMouseMove(LPARAM lp)
{
    if (!dragging_)
        return;

    current_.x = GET_X_LPARAM(lp);
    current_.y = GET_Y_LPARAM(lp);

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void RegionSelector::OnLeftButtonUp(LPARAM lp)
{
    dragging_ = false;

    current_.x = GET_X_LPARAM(lp);
    current_.y = GET_Y_LPARAM(lp);

    ReleaseCapture();

    result_.left = start_.x;
    result_.top = start_.y;
    result_.right = current_.x;
    result_.bottom = current_.y;

    done_ = true;
    DestroyWindow(hwnd_);
}

void RegionSelector::OnKeyDown(WPARAM wp)
{
    if (wp != VK_ESCAPE)
        return;

    cancelled_ = true;
    done_ = true;
    DestroyWindow(hwnd_);
}

void RegionSelector::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);

    RECT client;
    GetClientRect(hwnd_, &client);

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    if (dragging_)
    {
        int left = std::min(start_.x, current_.x);
        int top = std::min(start_.y, current_.y);
        int right = std::max(start_.x, current_.x);
        int bottom = std::max(start_.y, current_.y);

        HPEN pen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
        HGDIOBJ oldPen = SelectObject(hdc, pen);

        HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
        HGDIOBJ oldBrush = SelectObject(hdc, hollow);

        Rectangle(hdc, left, top, right, bottom);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK RegionSelector::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RegionSelector* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<RegionSelector*>(cs->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        self->hwnd_ = hwnd;
    }
    else
        self = reinterpret_cast<RegionSelector*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    

    if (!self)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_LBUTTONDOWN:
        self->OnLeftButtonDown(lp);
        return 0;

    case WM_MOUSEMOVE:
        self->OnMouseMove(lp);
        return 0;

    case WM_LBUTTONUP:
        self->OnLeftButtonUp(lp);
        return 0;

    case WM_KEYDOWN:
        self->OnKeyDown(wp);
        return 0;

    case WM_PAINT:
        self->OnPaint();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}