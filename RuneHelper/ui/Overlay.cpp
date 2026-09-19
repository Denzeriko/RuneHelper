#include "ui/Overlay.h"

#include <algorithm>
#include <utility>

#include "platform/OverlayBackend.h"

OverlayWindow::OverlayWindow()
    : backend_(CreateOverlayBackend())
{
}

OverlayWindow::~OverlayWindow()
{
    Shutdown();
}

bool OverlayWindow::Create()
{
    if (!backend_)
        return false;

    state_.running = backend_->Init("RuneHelperOverlay", 800, 600);
    dirty_ = true;

    if (state_.running)
    {
        backend_->SetAlwaysOnTop(state_.alwaysOnTop);
        backend_->SetClickThrough(state_.clickThrough);
        backend_->SetVisible(state_.visible);
    }

    return state_.running;
}

void OverlayWindow::Shutdown()
{
    state_.running = false;

    if (backend_)
        backend_->Shutdown();
}

void OverlayWindow::BringToTop()
{
    state_.alwaysOnTop = true;

    if (backend_)
        backend_->BringToTop();
}

void OverlayWindow::SetRegionPreview(bool enabled, const OverlayRect& rect)
{
    bool sameRect =
        state_.previewRect.left == rect.left &&
        state_.previewRect.top == rect.top &&
        state_.previewRect.right == rect.right &&
        state_.previewRect.bottom == rect.bottom;

    if (state_.previewEnabled == enabled && sameRect)
        return;

    state_.previewEnabled = enabled;
    state_.previewRect = rect;
    dirty_ = true;
}

void OverlayWindow::SetFrame(OverlayFrame frame)
{
    const bool sameTexts =
        state_.texts.size() == frame.texts.size() &&
        std::equal(
            state_.texts.begin(),
            state_.texts.end(),
            frame.texts.begin(),
            [](const OverlayText& a, const OverlayText& b)
            {
                return a.x == b.x
                    && a.y == b.y
                    && a.fontSize == b.fontSize
                    && a.color == b.color
                    && a.text == b.text;
            });

    const bool sameMarks =
        state_.marks.size() == frame.marks.size() &&
        std::equal(
            state_.marks.begin(),
            state_.marks.end(),
            frame.marks.begin(),
            [](const OverlayMark& a, const OverlayMark& b)
            {
                return a.x == b.x
                    && a.y == b.y
                    && a.width == b.width
                    && a.height == b.height
                    && a.color == b.color;
            });

    if (sameTexts && sameMarks)
        return;

    state_.texts = std::move(frame.texts);
    state_.marks = std::move(frame.marks);
    dirty_ = true;
}

void OverlayWindow::SetFontSize(int size)
{
    if (size <= 0 || state_.fontSize == size)
        return;

    state_.fontSize = size;
    dirty_ = true;
}

void OverlayWindow::SetBackground(bool enabled)
{
    if (state_.background == enabled)
        return;

    state_.background = enabled;
    dirty_ = true;
}

void OverlayWindow::SetOutline(bool enabled)
{
    if (state_.outline == enabled)
        return;

    state_.outline = enabled;
    dirty_ = true;
}

void OverlayWindow::SetFontSizeForce(int size)
{
    if (size <= 0)
        return;

    state_.fontSize = size;
    dirty_ = true;
}

void OverlayWindow::PumpMessages()
{
    if (!backend_ || !state_.running)
        return;

    backend_->PumpEvents();

    if (!backend_->IsRunning())
    {
        state_.running = false;
        return;
    }

    if (!dirty_ && !backend_->NeedsRedraw())
        return;

    dirty_ = false;
    backend_->Render(state_);
}
