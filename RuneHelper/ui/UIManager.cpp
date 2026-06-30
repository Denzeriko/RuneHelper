#include "ui/UIManager.h"

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
    return backend_ && backend_->Init(this);
}

void UIManager::Shutdown()
{
    if (backend_)
        backend_->Shutdown();
}

void UIManager::Pump()
{
    if (!backend_ || !backend_->BeginFrame())
        return;

    UIDraw::Draw(*this);
    backend_->EndFrame();
}

bool UIManager::IsRunning() const
{
    return backend_ && backend_->IsRunning();
}

void UIManager::SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed)
{
    ocrInitializing_ = ocrInitializing;
    ocrReady_ = ocrReady;
    ocrFailed_ = ocrFailed;
}

void UIManager::SetPriceStatus(bool downloading, size_t priceCount)
{
    priceDownloading_ = downloading;
    priceCount_ = priceCount;
}

void UIManager::SetUpdateChecker(UpdateChecker* checker)
{
    updateChecker_ = checker;
}

bool UIManager::WantsSelectRegion()
{
    bool value = wantsSelectRegion_;
    wantsSelectRegion_ = false;
    return value;
}

bool UIManager::WantsRefreshPrices()
{
    bool value = wantsRefreshPrices_;
    wantsRefreshPrices_ = false;
    return value;
}

bool UIManager::WantsToggleOCR()
{
    bool value = wantsToggleOCR_;
    wantsToggleOCR_ = false;
    return value;
}

bool UIManager::WantsSingleSnapshot()
{
    bool value = wantsSingleSnapshot_;
    wantsSingleSnapshot_ = false;
    return value;
}

bool UIManager::WantsTestOcr()
{
    bool value = wantsTestOcr_;
    wantsTestOcr_ = false;
    return value;
}

bool UIManager::WantsResetOcr()
{
    bool value = wantsResetOcr_;
    wantsResetOcr_ = false;
    return value;
}

bool UIManager::IsRegionHovered() const
{
    return regionHovered_;
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
    wantsToggleOCR_ = true;
}

void UIManager::RequestSingleSnapshot()
{
    wantsSingleSnapshot_ = true;
}

void UIManager::RequestSelectRegion()
{
    wantsSelectRegion_ = true;
}

void UIManager::RequestExit()
{
    if (backend_)
        backend_->RequestClose();
}

void UIManager::MarkSaved()
{
    showSaved_ = true;
    savedAt_ = std::chrono::steady_clock::now();
}
