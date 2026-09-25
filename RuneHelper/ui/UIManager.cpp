#include "ui/UIManager.h"

#include <utility>

#include "core/ConfigManager.h"
#include "platform/UIBackend.h"
#include "ui/UIDraw.h"

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
