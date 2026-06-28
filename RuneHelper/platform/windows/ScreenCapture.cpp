#include "ScreenCapture.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

cv::Mat CaptureScreen()
{
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScreen = GetDC(nullptr);
    HDC hDC = CreateCompatibleDC(hScreen);

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
    HGDIOBJ oldObj = SelectObject(hDC, hBitmap);

    BitBlt(hDC, 0, 0, width, height, hScreen, x, y, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    cv::Mat mat(height, width, CV_8UC3);
    GetDIBits(hDC, hBitmap, 0, height, mat.data, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hDC, oldObj);
    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(nullptr, hScreen);

    return mat;
}

cv::Mat CaptureScreen2(const cv::Rect& region)
{
    HDC hScreen = GetDC(nullptr);
    HDC hDC = CreateCompatibleDC(hScreen);

    int width = region.width;
    int height = region.height;

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
    HGDIOBJ oldObj = SelectObject(hDC, hBitmap);

    BitBlt(hDC, 0, 0, width, height, hScreen, region.x, region.y, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    cv::Mat mat(height, width, CV_8UC3);

    GetDIBits(hDC, hBitmap, 0, height, mat.data, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hDC, oldObj);
    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(nullptr, hScreen);

    return mat;
}

cv::Mat CaptureRegion(const cv::Rect& region)
{
    cv::Mat screen = CaptureScreen();
    //cv::Mat screen = CaptureScreen(region);

    cv::Rect screenRect(0, 0, screen.cols, screen.rows);
    cv::Rect safeRegion = region & screenRect;

    if (safeRegion.empty())
        return {};

    return screen(safeRegion).clone();
}