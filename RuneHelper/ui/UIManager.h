#pragma once

#include <windows.h>
#include <d3d11.h>

#include "core/Config.h"
#include "core/ConfigManager.h"
#include "core/UpdateChecker.h"
#include "ui/UIState.h"
#include "core/DebugData.h"

class UIManager
{
public:
    UIManager() = default;
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool Init(AppConfig* config, ConfigManager* configManager);

    void Shutdown();
    void Pump();

    bool IsRunning() const;

    void SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed);

    void SetPriceStatus(bool downloading, size_t priceCount);

    void SetUpdateChecker(UpdateChecker* checker);

    bool WantsSelectRegion();
    bool WantsRefreshPrices();
    bool WantsToggleOCR();
    bool WantsSingleSnapshot();
    bool WantsOCRRebuild();

    bool IsRegionHovered() const;

    void RegisterHotkeys();
    void UnregisterHotkeys();

    void SetDebugData(const DebugData& data);

private:
    bool CreateAppWindow();
    bool CreateDeviceD3D();

    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    void Render();

    void DrawTitleBar();
    void DrawSettings();
    void DrawMainTab();
    void DrawDebugTab();

    void DrawStatusSection();
    void DrawRegionSection();
    void DrawOcrSection();
    void DrawHotkeysSection();
    void DrawOverlaySection();
    void DrawPricesSection();
    void DrawBottomSection();

    void DrawHotkeyButton(const char* label, int& key);

    static bool IsMouseVk(int vk);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    HWND hwnd_ = nullptr;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTargetView_ = nullptr;

    AppConfig* config_ = nullptr;
    ConfigManager* configManager_ = nullptr;
    UpdateChecker* updateChecker_ = nullptr;

    UIState state_;

    DebugData debugData_;

    int* waitingForHotkey_ = nullptr;
    bool hotkeyCaptureSkipFrame_ = false;
};