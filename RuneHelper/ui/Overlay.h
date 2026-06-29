#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include "platform/linux/PlatformTypes.h"
#endif
#include <string>
#include <vector>

struct OverlayText
{
    std::wstring text;
    int x = 0;
    int y = 0;
    COLORREF color = RGB(255, 255, 255);
};

class OverlayWindow
{
public:
    bool Create();
    void BringToTop();
    void SetRegionPreview(bool enabled, const RECT& rect);
    void SetTexts(std::vector<OverlayText> texts);
    void SetFontSize(int size);
    void SetFontSizeForce(int size);
    void PumpMessages();

private:
#ifdef _WIN32
    HWND hwnd_ = nullptr;
#endif

    bool previewEnabled_ = false;
    RECT previewRect_{};

#ifdef _WIN32
    HFONT font_ = nullptr;
#endif
    int fontSize_ = 24;

    std::vector<OverlayText> texts_;

private:
#ifdef _WIN32
    void RecreateFont();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
#endif
};
