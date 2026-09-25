#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "core/Config.h"
#include "core/DebugData.h"
#include "ui/UIState.h"

class ConfigManager;
class FeatureRegistry;
class UIBackend;
class UpdateChecker;

class UIManager
{
public:
    UIManager(ConfigManager& config, const UpdateChecker& updates, FeatureRegistry& features);
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool Init();
    void Shutdown();
    void Pump();

    bool IsRunning() const;

    UIState& State() { return state_; }

    UIRequests TakeRequests() { return std::exchange(state_.requests, {}); }

    AppConfig& ConfigDraft() { return configDraft_; }

    void ApplyConfigDraft();

    const UpdateChecker& Updates() const { return updates_; }

    FeatureRegistry& Features() { return features_; }

    void SetDebugData(DebugData data);

    const DebugData& GetDebugData() const { return debugData_; }

    std::uint64_t DebugDataVersion() const { return debugDataVersion_; }

    bool NeedsDebugData() const { return state_.debugTabOpen || state_.featureTabOpen; }

    std::string HotkeyToString(int key) const;
    bool CaptureNextHotkey(int& key);
    void RegisterHotkeys();
    void UnregisterHotkeys();

    void Minimize();
    void Exit();

private:
    ConfigManager& config_;
    const UpdateChecker& updates_;
    FeatureRegistry& features_;

    AppConfig configDraft_;
    UIState state_;
    DebugData debugData_;
    std::uint64_t debugDataVersion_ = 0;

    std::unique_ptr<UIBackend> backend_;
};
