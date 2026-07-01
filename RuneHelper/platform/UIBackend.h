#pragma once

#include <string>

class UIManager;

class UIBackend
{
public:
    UIBackend();
    ~UIBackend();

    UIBackend(const UIBackend&) = delete;
    UIBackend& operator=(const UIBackend&) = delete;

    bool Init(UIManager* manager);
    void Shutdown();

    void SetupStyle();

    bool BeginFrame();
    void EndFrame();

    bool IsRunning() const;
    void RequestClose();

    std::string HotkeyToString(int key) const;
    bool CaptureNextHotkey(int& key);

    void RegisterHotkeys(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey);
    void UnregisterHotkeys();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
