#pragma once
#include <windows.h>
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
    void SetTexts(std::vector<OverlayText> texts);
    void SetFontSize(int size);
    void SetFontSizeForce(int size);
    void PumpMessages();

private:
    HWND hwnd_ = nullptr;

    HFONT font_ = nullptr;
    int fontSize_ = 24;

    std::vector<OverlayText> texts_;

private:
    void RecreateFont();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};