#pragma once

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
    void RequestClose();

    void RegisterHotkeys(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey);
    void UnregisterHotkeys();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
