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

class UIManager
{
public:
    UIManager() = default;
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool Init(AppConfig* config, ConfigManager* configManager);
    void Shutdown();
    void SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed);

    void Pump();

    bool IsRunning() const;
    void SetUpdateChecker(UpdateChecker* checker);
    bool WantsSelectRegion();
    bool WantsRefreshPrices();
#ifndef _WIN32
    bool WantsTestOcr();
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

    AppConfig* config_ = nullptr;
    ConfigManager* configManager_ = nullptr;
    UpdateChecker* updateChecker_ = nullptr;

    bool ocrInitializing_ = false;
    bool ocrReady_ = false;
    bool ocrFailed_ = false;

    bool running_ = false;
    bool wantsSelectRegion_ = false;
    bool wantsRefreshPrices_ = false;
#ifndef _WIN32
    bool wantsTestOcr_ = false;
#endif
    bool regionHovered_ = false;

    bool priceDownloading_ = false;
    size_t priceCount_ = 0;

    bool showSaved_ = false;
    std::chrono::steady_clock::time_point savedAt_;

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

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
#endif
};
