#pragma once

#include <memory>

struct OverlayState;

class OverlayBackend
{
public:
    virtual ~OverlayBackend() = default;

    virtual bool Init(const char* title, int width, int height) = 0;
    virtual void Shutdown() = 0;

    virtual bool IsRunning() const = 0;
    virtual void PumpEvents() = 0;
    virtual void Render(const OverlayState& state) = 0;

    virtual void SetVisible(bool visible) = 0;
    virtual void SetClickThrough(bool enabled) = 0;
    virtual void SetAlwaysOnTop(bool enabled) = 0;
    virtual void BringToTop() = 0;
};

std::unique_ptr<OverlayBackend> CreateOverlayBackend();
