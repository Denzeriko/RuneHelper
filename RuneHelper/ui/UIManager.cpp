#include "ui/UIManager.h"

#include <algorithm>
#include <utility>

#include "core/ConfigManager.h"
#include "platform/UIBackend.h"
#include "ui/UIDraw.h"
#include "ui/OverlayRenderer.h"

UIManager::UIManager(ConfigManager& config, const UpdateChecker& updates, FeatureRegistry& features)
    : config_(config), updates_(updates), features_(features), backend_(std::make_unique<UIBackend>())
{
}

UIManager::~UIManager()
{
    Shutdown();
}

bool UIManager::Init()
{
    configDraft_ = config_.Snapshot();
    state_.running = backend_->Init(this);
    return state_.running;
}

void UIManager::Shutdown()
{
    state_.running = false;
    previewPanel_.reset();
    previewImage_ = {};
    backend_->Shutdown();
}

void UIManager::Pump()
{
    if (!backend_->BeginFrame())
    {
        state_.running = backend_->IsRunning();
        return;
    }

    configDraft_ = config_.Snapshot();

    UIDraw::Draw(*this);
    backend_->EndFrame();
}

bool UIManager::IsRunning() const
{
    return state_.running && backend_->IsRunning();
}

void UIManager::EnqueueCommand(UICommand command)
{
    if (std::find(commands_.begin(), commands_.end(), command) == commands_.end())
        commands_.push_back(command);
}

void UIManager::ApplyConfigDraft()
{
    config_.Update([this](AppConfig& config) { config = configDraft_; });

    configDraft_ = config_.Snapshot();
}

void UIManager::SetDebugData(DebugData data)
{
    debugData_ = std::move(data);
    ++debugDataVersion_;
}

std::string UIManager::HotkeyToString(int key) const
{
    return backend_->HotkeyToString(key);
}

bool UIManager::CaptureNextHotkey(int& key)
{
    return backend_->CaptureNextHotkey(key);
}

void UIManager::RegisterHotkeys()
{
    const AppConfig config = config_.Snapshot();

    backend_->RegisterHotkeys(config.hotkeyToggleOCR, config.hotkeySingleSnapshot, config.hotkeySelectRegion);
}

void UIManager::UnregisterHotkeys()
{
    backend_->UnregisterHotkeys();
}

void UIManager::Minimize()
{
    backend_->Minimize();
}

void UIManager::Exit()
{
    state_.running = false;
    backend_->RequestClose();
}

#ifdef _WIN32
void UIManager::MinimizeToTray()
{
    backend_->MinimizeToTray();
}
#endif

OverlayPreview UIManager::PreviewImage(const OverlayPanel& panel, bool textRows)
{
    if (previewPanel_ && *previewPanel_ == panel && previewTextRows_ == textRows)
        return previewImage_;

    OverlayPanel fitted = panel;
    if (!textRows)
        fitted.width = std::min(panel.width, OverlayRenderer::PanelContentWidth(panel));
    fitted.height = OverlayRenderer::PanelContentHeight(fitted);
    const int height = std::max(panel.height, fitted.height);

    OverlayState preview;
    preview.fontSize = panel.fontSize;
    preview.background = panel.background;
    preview.outline = panel.outline;

    if (textRows)
    {
        int y = panel.fontSize;

        for (const auto& line : panel.lines)
        {
            preview.texts.push_back({ line.text, 12, y, line.color });
            y += panel.fontSize * 3 / 2 + 8;
        }
    }
    else
        preview.panels.push_back(std::move(fitted));

    cv::Mat image(height, panel.width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    OverlayRenderer::Paint(image, cv::Point(), preview);
    previewImage_ = { backend_->UpdatePreview(image), height };
    previewPanel_ = panel;
    previewTextRows_ = textRows;
    return previewImage_;
}
