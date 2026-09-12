#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

#include "core/Config.h"
#include "core/ConfigManager.h"
#include "core/DebugData.h"
#include "core/UpdateChecker.h"
#include "ocr/RunePatternMatcher.h"
#include "ui/UIState.h"

class UIBackend;
class UIManager;

namespace UIDraw
{
void Draw(UIManager& manager);
void DrawTitleBar(UIManager& manager, UIState& state);
void DrawMainTab(UIManager& manager, UIState& state);
void DrawDebugTab(UIManager& manager, UIState& state);
}

class UIManager
{
public:
    UIManager();
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool Init(AppConfig* config, ConfigManager* configManager);

    void Shutdown();
    void Pump();

    bool IsRunning() const;

    void SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed);
    void SetPriceStatus(bool downloading, size_t priceCount);
    void SetRuneCalibrationStatus(const RunePatternCalibrationStatus& status);
    void SetUpdateChecker(UpdateChecker* checker);
    bool IsCheckingForUpdate() const;
    bool HasUpdate() const;
    std::string UpdateDownloadUrl() const;

    bool HasConfig() const;
    AppConfig& Config();
    std::mutex& ConfigMutex() const;
    UIState& State();

    bool WantsSelectRegion();
    bool WantsRefreshPrices();
    bool WantsToggleOCR();
    bool WantsSingleSnapshot();
    bool WantsRegisterHotkeys();
    bool WantsCalibrateRunes();

    bool IsRegionHovered() const;

    std::string HotkeyToString(int key) const;
    bool CaptureNextHotkey(int& key);
    bool SaveConfig();

    void RegisterHotkeys();
    void UnregisterHotkeys();

    void SetDebugData(DebugData data);
    const DebugData& GetDebugData() const;
    bool IsDebugTabOpen() const;
    void FlushPendingConfigSave();

    void RequestToggleOCR();
    void RequestSingleSnapshot();
    void RequestSelectRegion();
    void RequestRegisterHotkeys();
    void RequestMinimize();
    void RequestExit();

private:
    AppConfig* config_ = nullptr;
    ConfigManager* configManager_ = nullptr;
    UpdateChecker* updateChecker_ = nullptr;

    UIState state_;
    DebugData debugData_;

    std::unique_ptr<UIBackend> backend_;
};
