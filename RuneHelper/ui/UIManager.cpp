#include "ui/UIManager.h"

#include <mutex>
#include <utility>

#include "core/Logger.h"

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
    FlushPendingConfigSave();

    state_.running = false;

    if (backend_)
        backend_->Shutdown();
}

void UIManager::Pump()
{
    if (!backend_)
    {
        state_.running = false;
        return;
    }

    if (!backend_->BeginFrame())
    {
        state_.running = backend_->IsRunning();
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

void UIManager::SetRuneCalibrationStatus(const RunePatternCalibrationStatus& status)
{
    state_.runeCalibrationStatus = status;
}

void UIManager::SetUpdateChecker(UpdateChecker* checker)
{
    updateChecker_ = checker;
}

bool UIManager::IsCheckingForUpdate() const
{
    return updateChecker_ && updateChecker_->IsChecking();
}

bool UIManager::HasUpdate() const
{
    return updateChecker_ && updateChecker_->HasUpdate();
}

std::string UIManager::UpdateDownloadUrl() const
{
    return updateChecker_ ? updateChecker_->DownloadUrl() : "";
}

bool UIManager::HasConfig() const
{
    return config_ && configManager_;
}

AppConfig& UIManager::Config()
{
    return *config_;
}

std::mutex& UIManager::ConfigMutex() const
{
    return configManager_->Mutex();
}

UIState& UIManager::State()
{
    return state_;
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

bool UIManager::WantsRegisterHotkeys()
{
    return std::exchange(state_.wantsRegisterHotkeys, false);
}

bool UIManager::WantsCalibrateRunes()
{
    return std::exchange(state_.wantsCalibrateRunes, false);
}

bool UIManager::IsRegionHovered() const
{
    return state_.regionHovered;
}

std::string UIManager::HotkeyToString(int key) const
{
    if (!backend_)
        return "None";

    return backend_->HotkeyToString(key);
}

bool UIManager::CaptureNextHotkey(int& key)
{
    return backend_ && backend_->CaptureNextHotkey(key);
}

bool UIManager::SaveConfig()
{
    return configManager_ && configManager_->Save();
}


void UIManager::RegisterHotkeys()
{
    if (backend_ && config_ && configManager_)
    {
        AppConfig config;
        {
            std::lock_guard lock(configManager_->Mutex());
            config = *config_;
        }

        backend_->RegisterHotkeys(
            config.hotkeyToggleOCR,
            config.hotkeySingleSnapshot,
            config.hotkeySelectRegion
        );
    }
}

void UIManager::UnregisterHotkeys()
{
    if (backend_)
        backend_->UnregisterHotkeys();
}

void UIManager::SetDebugData(DebugData data)
{
    debugData_ = std::move(data);
}

const DebugData& UIManager::GetDebugData() const
{
    return debugData_;
}

bool UIManager::IsDebugTabOpen() const
{
    return state_.debugTabOpen;
}

void UIManager::FlushPendingConfigSave()
{
    if (!state_.configSavePending || !configManager_)
        return;

    state_.configSavePending = false;

    std::lock_guard lock(configManager_->Mutex());

    if (!configManager_->Save())
        LOG_ERROR("UI failed to save pending config");
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

void UIManager::RequestRegisterHotkeys()
{
    state_.wantsRegisterHotkeys = true;
}

void UIManager::RequestMinimize()
{
    if (backend_)
        backend_->Minimize();
}

void UIManager::RequestExit()
{
    state_.running = false;

    if (backend_)
        backend_->RequestClose();
}
