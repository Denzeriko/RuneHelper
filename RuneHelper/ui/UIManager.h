#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Config.h"
#include "core/DebugData.h"
#include "ui/UICommand.h"
#include "ui/UIState.h"
#include "ui/OverlayState.h"

class ConfigManager;
class FeatureRegistry;
class UIBackend;
class UpdateChecker;

struct OverlayPreview
{
    std::uintptr_t texture = 0;
    int height = 0;
};

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

    void EnqueueCommand(UICommand command);

    std::vector<UICommand> TakeCommands() { return std::exchange(commands_, {}); }

    AppConfig& ConfigDraft() { return configDraft_; }

    void ApplyConfigDraft();
    OverlayPreview PreviewImage(const OverlayPanel& panel, bool textRows);

    const UpdateChecker& Updates() const { return updates_; }

    FeatureRegistry& Features() { return features_; }

    void SetDebugData(DebugData data);

    const DebugData& GetDebugData() const { return debugData_; }

    std::uint64_t DebugDataVersion() const { return debugDataVersion_; }

    bool NeedsDebugData() const { return state_.page == UIPage::Diagnostics; }

    std::string HotkeyToString(int key) const;
    bool CaptureNextHotkey(int& key);
    void RegisterHotkeys();
    void UnregisterHotkeys();

    void Minimize();
#ifdef _WIN32
    void MinimizeToTray();
#endif
    void Exit();

private:
    ConfigManager& config_;
    const UpdateChecker& updates_;
    FeatureRegistry& features_;

    AppConfig configDraft_;
    UIState state_;
    std::vector<UICommand> commands_;
    DebugData debugData_;
    std::uint64_t debugDataVersion_ = 0;

    std::optional<OverlayPanel> previewPanel_;
    bool previewTextRows_ = false;
    OverlayPreview previewImage_;
    std::unique_ptr<UIBackend> backend_;
};
