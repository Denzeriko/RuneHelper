#include "ScreenCapture.h"

#include <windows.h>

#include <opencv2/imgproc.hpp>

cv::Mat CaptureRegion(const cv::Rect& region)
{
    if (region.width <= 0 || region.height <= 0)
        return {};

    const cv::Rect virtualScreen(
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_CYVIRTUALSCREEN)
    );

    const cv::Rect safeRegion = region & virtualScreen;

    if (safeRegion.empty())
        return {};

    HDC screenDC = GetDC(nullptr);

    if (!screenDC)
        return {};

    HDC memoryDC = CreateCompatibleDC(screenDC);

    if (!memoryDC)
    {
        ReleaseDC(nullptr, screenDC);
        return {};
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, safeRegion.width, safeRegion.height);

    if (!bitmap)
    {
        DeleteDC(memoryDC);
        ReleaseDC(nullptr, screenDC);
        return {};
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDC, bitmap);

    const BOOL blitted = BitBlt(
        memoryDC,
        0,
        0,
        safeRegion.width,
        safeRegion.height,
        screenDC,
        safeRegion.x,
        safeRegion.y,
        SRCCOPY
    );

    SelectObject(memoryDC, oldBitmap);

    cv::Mat result;

    if (blitted)
    {
        BITMAPINFOHEADER header{};
        header.biSize = sizeof(BITMAPINFOHEADER);
        header.biWidth = safeRegion.width;
        header.biHeight = -safeRegion.height;
        header.biPlanes = 1;
        header.biBitCount = 32;
        header.biCompression = BI_RGB;

        cv::Mat bgra(safeRegion.height, safeRegion.width, CV_8UC4);

        if (GetDIBits(
                memoryDC,
                bitmap,
                0,
                static_cast<UINT>(safeRegion.height),
                bgra.data,
                reinterpret_cast<BITMAPINFO*>(&header),
                DIB_RGB_COLORS))
        {
            cv::cvtColor(bgra, result, cv::COLOR_BGRA2BGR);
        }
    }

    DeleteObject(bitmap);
    DeleteDC(memoryDC);
    ReleaseDC(nullptr, screenDC);

    return result;
}
