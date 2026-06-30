#pragma once

#include <windows.h>
#include <opencv2/opencv.hpp>

class RegionSelector
{
public:
    RegionSelector() = default;
    ~RegionSelector();

    RegionSelector(const RegionSelector&) = delete;
    RegionSelector& operator=(const RegionSelector&) = delete;

    cv::Rect Select();

private:
    HWND hwnd_ = nullptr;

    POINT start_{};
    POINT current_{};

    bool dragging_ = false;
    bool done_ = false;
    bool cancelled_ = false;

    RECT result_{};

    int virtualX_ = 0;
    int virtualY_ = 0;

private:
    bool CreateOverlayWindow();
    void DestroyOverlayWindow();

    cv::Rect GetSelectedRect() const;

    void OnLeftButtonDown(LPARAM lp);
    void OnMouseMove(LPARAM lp);
    void OnLeftButtonUp(LPARAM lp);
    void OnKeyDown(WPARAM wp);
    void OnPaint();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};