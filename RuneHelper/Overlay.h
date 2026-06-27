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
    void PumpMessages();
private:
    HWND hwnd_ = nullptr;
    std::vector<OverlayText> texts_;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};