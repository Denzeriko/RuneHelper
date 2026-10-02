#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace cv
{
class Mat;
}

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
    std::uintptr_t UpdatePreview(const cv::Mat& image);

    bool IsRunning() const;
    void Minimize();
#ifdef _WIN32
    void MinimizeToTray();
#endif
    void RequestClose();

    std::string HotkeyToString(int key) const;
    bool CaptureNextHotkey(int& key);

    void RegisterHotkeys(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey);
    void UnregisterHotkeys();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
