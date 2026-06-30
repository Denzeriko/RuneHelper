#pragma once

#include <memory>
#include <vector>

#include "ui/OverlayState.h"

class OverlayBackend;

class OverlayWindow
{
public:
    OverlayWindow();
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create();
    void Shutdown();

    void BringToTop();
    void SetRegionPreview(bool enabled, const OverlayRect& rect);
    void SetTexts(std::vector<OverlayText> texts);
    void SetFontSize(int size);
    void SetFontSizeForce(int size);
    void PumpMessages();

private:
    OverlayState state_;
    std::unique_ptr<OverlayBackend> backend_;
};
