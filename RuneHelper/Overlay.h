#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct OverlayText
{
    std::wstring text;
    int x;
    int y;
};

class OverlayWindow
{
public:
    bool Create();
    void SetTexts(std::vector<OverlayText> texts);
    void SetFontSize(int size);
    void PumpMessages();

private:
    HWND hwnd_ = nullptr;

    HFONT font_ = nullptr;
    int fontSize_ = 22;

    std::vector<OverlayText> texts_;

private:
    void RecreateFont();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};