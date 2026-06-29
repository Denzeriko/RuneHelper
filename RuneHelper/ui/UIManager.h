#pragma once

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#endif

#include <chrono>
#include <cstddef>

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
    void SetUpdateChecker(UpdateChecker* checker);
    bool WantsSelectRegion();
    bool WantsRefreshPrices();
#ifndef _WIN32
    bool WantsTestOcr();
    bool WantsResetOcr();
#endif
    bool IsRegionHovered() const;
    void SetPriceStatus(bool downloading, size_t priceCount);

private:
#ifdef _WIN32
    HWND hwnd_ = nullptr;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTargetView_ = nullptr;
#endif

    void SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed);

    void SetPriceStatus(bool downloading, size_t priceCount);

    void SetUpdateChecker(UpdateChecker* checker);

    bool WantsSelectRegion();
    bool WantsRefreshPrices();
    bool WantsToggleOCR();
    bool WantsSingleSnapshot();

    bool running_ = false;
    bool wantsSelectRegion_ = false;
    bool wantsRefreshPrices_ = false;
#ifndef _WIN32
    bool wantsTestOcr_ = false;
    bool wantsResetOcr_ = false;
#endif
    bool regionHovered_ = false;
    bool IsRegionHovered() const;

    void RegisterHotkeys();
    void UnregisterHotkeys();

    void SetDebugData(const DebugData& data);

private:
#ifdef _WIN32
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
#endif
};
