#include "ui/UIManager.h"

#include <utility>

#include "platform/UIBackend.h"
#include "ui/UIDraw.h"

UIManager::UIManager()
    : backend_(std::make_unique<UIBackend>())
{
}

UIManager::~UIManager()
{
    Shutdown();
}

bool UIManager::Init(AppConfig* config, ConfigManager* configManager)
{
    config_ = config;
    configManager_ = configManager;
    state_.running = backend_ && backend_->Init(this);
    return state_.running;
}

void UIManager::Shutdown()
{
    state_.running = false;

    if (backend_)
        backend_->Shutdown();
}

void UIManager::Pump()
{
    if (!backend_ || !backend_->BeginFrame())
    {
        state_.running = false;
        return;
    }

    UIDraw::Draw(*this);
    backend_->EndFrame();
}

bool UIManager::IsRunning() const
{
    return state_.running && backend_ && backend_->IsRunning();
}

void UIManager::SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed)
{
    state_.ocrInitializing = ocrInitializing;
    state_.ocrReady = ocrReady;
    state_.ocrFailed = ocrFailed;
}

void UIManager::SetPriceStatus(bool downloading, size_t priceCount)
{
    state_.priceDownloading = downloading;
    state_.priceCount = priceCount;
}

void UIManager::SetUpdateChecker(UpdateChecker* checker)
{
    updateChecker_ = checker;
}

bool UIManager::WantsSelectRegion()
{
    return std::exchange(state_.wantsSelectRegion, false);
}

bool UIManager::WantsRefreshPrices()
{
    return std::exchange(state_.wantsRefreshPrices, false);
}

bool UIManager::WantsToggleOCR()
{
    return std::exchange(state_.wantsToggleOCR, false);
}

bool UIManager::WantsSingleSnapshot()
{
    return std::exchange(state_.wantsSingleSnapshot, false);
}

bool UIManager::WantsTestOcr()
{
    return std::exchange(state_.wantsTestOcr, false);
}

bool UIManager::WantsResetOcr()
{
    return std::exchange(state_.wantsResetOcr, false);
}

bool UIManager::WantsOCRRebuild()
{
    return std::exchange(state_.wantsOCRRebuild, false);
}

bool UIManager::IsRegionHovered() const
{
    return state_.regionHovered;
}

void UIManager::RegisterHotkeys()
{
    if (backend_ && config_)
    {
        backend_->RegisterHotkeys(
            config_->hotkeyToggleOCR,
            config_->hotkeySingleSnapshot,
            config_->hotkeySelectRegion
        );
    }
}

void UIManager::UnregisterHotkeys()
{
    if (backend_)
        backend_->UnregisterHotkeys();
}

void UIManager::SetDebugData(const DebugData& data)
{
    debugData_ = data;
}

void UIManager::RequestToggleOCR()
{
    state_.wantsToggleOCR = true;
}

void UIManager::RequestSingleSnapshot()
{
    state_.wantsSingleSnapshot = true;
}

void UIManager::RequestSelectRegion()
{
    state_.wantsSelectRegion = true;
}

void UIManager::RequestExit()
{
    state_.running = false;

    if (backend_)
        backend_->RequestClose();
}

void UIManager::MarkSaved()
{
    state_.showSaved = true;
    state_.savedAt = std::chrono::steady_clock::now();
}
