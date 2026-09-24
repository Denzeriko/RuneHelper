#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "core/Config.h"
#include "core/ConfigManager.h"
#include "core/DebugData.h"
#include "core/UpdateChecker.h"
#include "ui/UIState.h"

class FeatureRegistry;
class UIBackend;

class UIManager
{
public:
    UIManager();
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool Init(ConfigManager* configManager);

    void Shutdown();
    void Pump();

    bool IsRunning() const;

    void SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed);
    void SetOverlayAvailable(bool available);
    void SetCaptureFailing(bool failing);
    void SetPriceStatus(bool downloading, size_t priceCount);
    void SetUpdateChecker(UpdateChecker* checker);
    void SetFeatures(FeatureRegistry* features);
    FeatureRegistry* Features() const;
    bool IsCheckingForUpdate() const;
    bool HasUpdate() const;
    std::string UpdateDownloadUrl() const;

    bool HasConfig() const;
    AppConfig& ConfigDraft();
    void ApplyConfigDraft();
    UIState& State();

    bool WantsSelectRegion();
    bool WantsRefreshPrices();
    bool WantsToggleOCR();
    bool WantsSingleSnapshot();
    bool WantsOcrDebug();
    bool WantsRegisterHotkeys();

    bool IsRegionHovered() const;

    std::string HotkeyToString(int key) const;
    bool CaptureNextHotkey(int& key);
    bool SaveConfig();

    void RegisterHotkeys();
    void UnregisterHotkeys();

    void SetDebugData(DebugData data);
    const DebugData& GetDebugData() const;
    std::uint64_t DebugDataVersion() const;
    bool NeedsDebugData() const;
    void FlushPendingConfigSave();

    void RequestToggleOCR();
    void RequestSingleSnapshot();
    void RequestOcrDebug();
    void RequestSelectRegion();
    void RequestRegisterHotkeys();
    void RequestMinimize();
    void RequestExit();

private:
    ConfigManager* configManager_ = nullptr;
    AppConfig configDraft_;
    UpdateChecker* updateChecker_ = nullptr;
    FeatureRegistry* features_ = nullptr;

    UIState state_;
    DebugData debugData_;
    std::uint64_t debugDataVersion_ = 0;

    std::unique_ptr<UIBackend> backend_;
};
