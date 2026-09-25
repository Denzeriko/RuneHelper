#pragma once

#include <memory>
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

    bool BeginFrame();
    void EndFrame();

    bool IsRunning() const;
    void Minimize();
    void RequestClose();

    std::string HotkeyToString(int key) const;
    bool CaptureNextHotkey(int& key);

    void RegisterHotkeys(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey);
    void UnregisterHotkeys();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
