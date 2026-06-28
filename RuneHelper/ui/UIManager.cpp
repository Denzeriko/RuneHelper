#include "UIManager.h"

#include <iostream>
#include <windowsx.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "core/Version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

UIManager::~UIManager()
{
    Shutdown();
}

bool UIManager::Init(AppConfig* config, ConfigManager* configManager)
{
    config_ = config;
    configManager_ = configManager;

    if (!CreateAppWindow())
        return false;

    if (!CreateDeviceD3D())
        return false;

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    //STYLE!
    ImGuiStyle& style = ImGui::GetStyle();

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);

    colors[ImGuiCol_TabActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);

    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    colors[ImGuiCol_SeparatorActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
    //

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, deviceContext_);

    running_ = true;

    return true;
}

void UIManager::Shutdown()
{
    if (!running_ && !hwnd_)
        return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    if (ImGui::GetCurrentContext())
        ImGui::DestroyContext();

    CleanupDeviceD3D();

    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    running_ = false;
}

void UIManager::SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed)
{
    ocrInitializing_ = ocrInitializing;
    ocrReady_ = ocrReady;
    ocrFailed_ = ocrFailed;
}

void UIManager::SetUpdateChecker(UpdateChecker* checker)
{
    updateChecker_ = checker;
}

bool UIManager::IsRunning() const
{
    return running_;
}

bool UIManager::WantsSelectRegion()
{
    bool value = wantsSelectRegion_;
    wantsSelectRegion_ = false;
    return value;
}

bool UIManager::WantsRefreshPrices()
{
    bool value = wantsRefreshPrices_;
    wantsRefreshPrices_ = false;
    return value;
}

bool UIManager::IsRegionHovered() const
{
    return regionHovered_;
}

void UIManager::Pump()
{
    if (!running_)
        return;

    MSG msg;

    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
            running_ = false;
    }

    if (!running_)
        return;

    Render();
}

bool UIManager::CreateAppWindow()
{
    HINSTANCE hInst = GetModuleHandle(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = UIManager::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RuneHelperSettingsWindow";

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowW(
        wc.lpszClassName,
        L"RuneHelper Settings",
        WS_POPUP,
        100,
        100,
        420,
        525,
        nullptr,
        nullptr,
        hInst,
        this
    );

    return hwnd_ != nullptr;
}

bool UIManager::CreateDeviceD3D()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &swapChain_,
        &device_,
        &featureLevel,
        &deviceContext_
    );

    if (FAILED(res))
        return false;

    CreateRenderTarget();

    return true;
}

void UIManager::CleanupDeviceD3D()
{
    CleanupRenderTarget();

    if (swapChain_)
    {
        swapChain_->Release();
        swapChain_ = nullptr;
    }

    if (deviceContext_)
    {
        deviceContext_->Release();
        deviceContext_ = nullptr;
    }

    if (device_)
    {
        device_->Release();
        device_ = nullptr;
    }
}

void UIManager::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;

    swapChain_->GetBuffer(
        0,
        IID_PPV_ARGS(&backBuffer)
    );

    device_->CreateRenderTargetView(
        backBuffer,
        nullptr,
        &renderTargetView_
    );

    backBuffer->Release();
}

void UIManager::CleanupRenderTarget()
{
    if (renderTargetView_)
    {
        renderTargetView_->Release();
        renderTargetView_ = nullptr;
    }
}

void UIManager::Render()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();

    DrawSettings();

    ImGui::Render();

    const float clearColor[4] =
    {
        0.08f,
        0.08f,
        0.08f,
        1.0f
    };

    deviceContext_->OMSetRenderTargets(
        1,
        &renderTargetView_,
        nullptr
    );

    deviceContext_->ClearRenderTargetView(
        renderTargetView_,
        clearColor
    );

    ImGui_ImplDX11_RenderDrawData(
        ImGui::GetDrawData()
    );

    swapChain_->Present(1, 0);
}

void UIManager::DrawTitleBar()
{
    const float titleBarHeight = 16;

    ImGui::BeginChild("TitleBar", ImVec2(0, titleBarHeight), false);

    ImGui::TextUnformatted("RuneHelper");
    ImGui::SameLine();
    ImGui::TextDisabled(RUNEHELPER_VERSION);

    ImGui::SameLine(ImGui::GetWindowWidth() - 40.0f);

    if (ImGui::Button("_", ImVec2(16, 16)))
        ShowWindow(hwnd_, SW_MINIMIZE);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));

    if (ImGui::Button("X", ImVec2(16, 16)))
    {
        running_ = false;
        DestroyWindow(hwnd_);
    }
    ImGui::PopStyleColor(3);

    ImGui::EndChild();
}

void UIManager::DrawSettings()
{
    if (!config_)
        return;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always );

    ImGui::Begin(
        "RuneHelper",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse
    );

    UIManager::DrawTitleBar();
    ImGui::Separator();
    //




    ImGui::Separator();

    ImGui::Text("Status");
    ImGui::Separator();

    ImGui::Text("OCR:");
    ImGui::SameLine();
    if (ocrInitializing_)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Initializing...");
    }
    else if (ocrFailed_)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Failed");
    }
    else if (ocrReady_)
    {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Ready");
    }
    else
    {
        ImGui::Text("Waiting...");
    }

    ImGui::Text("Version: %s", RUNEHELPER_VERSION);
    ImGui::SameLine();
    if (updateChecker_)
    {
        if (updateChecker_->IsChecking())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Checking for updates...");
        }
        else if (updateChecker_->HasUpdate())
        {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "New version available: %s", updateChecker_->LatestVersion().c_str());

            ImGui::SameLine();
            if (ImGui::Button("Download"))
            {
                ShellExecuteA(nullptr, "open", updateChecker_->DownloadUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "You are using the latest version");
        }
    }

    ImGui::Spacing();

    ImGui::Text("Region");
    ImGui::Separator();

    /*ImGui::Text(
        "x=%d y=%d w=%d h=%d",
        config_->regionX,
        config_->regionY,
        config_->regionW,
        config_->regionH
    );*/

    if (ImGui::Button("Select Region"))
        wantsSelectRegion_ = true;

    regionHovered_ = ImGui::IsItemHovered(); //show rectangle preview

    ImGui::SameLine();

    if (config_->regionW <= 0)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "No region selected");
    else
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Region selected");
       
    ImGui::Spacing();

    ImGui::Text("OCR");
    ImGui::Separator();

    if (config_->ocrEnabled)
    {
        if (ImGui::Button("Disable OCR"))
            config_->ocrEnabled = false;

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Enabled");
    }
    else
    {
        if (ImGui::Button("Enable OCR"))
            config_->ocrEnabled = true;

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Disabled");
    }

    ImGui::Checkbox("OCR AutoDetect Menu (Experemental)", &config_->ocrAutoDetect);

    //ImGui::SliderFloat("Scale", reinterpret_cast<float*>(&config_->ocrScale), 0.5f, 5.0f);

    ImGui::SliderFloat("OCR Threshold", reinterpret_cast<float*>(&config_->ocrThreshold), 0.0f,255.0f);

    ImGui::SliderInt("OCR interval ms", &config_->ocrIntervalMs, 100, 2000);

    //ImGui::Checkbox("Debug OCR", &config_->debugOCR);

    ImGui::Spacing();

    ImGui::Text("Overlay");
    ImGui::Separator();

    ImGui::SliderInt("Offset X", &config_->overlayOffsetX, -300, 500);

    ImGui::SliderInt("Offset Y", &config_->overlayOffsetY, -200, 200);

    ImGui::SliderInt("Font size", &config_->overlayFontSize, 8, 48);

    ImGui::Spacing();

    ImGui::Text("Prices");
    ImGui::Separator();

    ImGui::InputInt("Green     ex", &config_->priceColorMedium,    0, 5000);
    ImGui::InputInt("Yellow    ex", &config_->priceColorHigh,      0, 5000);
    ImGui::InputInt("Red       ex", &config_->priceColorVeryHigh,  0, 5000);
    ImGui::Separator();
    ImGui::SliderInt("Refresh minutes", &config_->priceRefreshMinutes, 1, 60);

    if (ImGui::Button("Refresh Prices Now"))
    {
        wantsRefreshPrices_ = true;
    }

    ImGui::Spacing();

    if (ImGui::Button("Save Config"))
    {
        if (configManager_)
        {
            configManager_->Save();

            showSaved_ = true;
            savedAt_ = std::chrono::steady_clock::now();
        }
    }

    if (showSaved_)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - savedAt_);

        if (elapsed < std::chrono::seconds(1))
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Saved!");
        }
        else
            showSaved_ = false;
    }

    ImGui::SameLine();

    if (ImGui::Button("Exit"))
    {
        running_ = false;
        PostQuitMessage(0);
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    const char* author = "Denz";
    float textWidth = ImGui::CalcTextSize(author).x;
    float textHeight = ImGui::CalcTextSize(author).y;

    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - textWidth - 15.0f, ImGui::GetWindowHeight() - textHeight - 10.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
    ImGui::TextDisabled("%s", author);
    ImGui::PopStyleColor();

    ImGui::End();
}

LRESULT CALLBACK UIManager::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;

    UIManager* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs =reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<UIManager*>(cs->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        self->hwnd_ = hwnd;
    }
    else
    {
        self = reinterpret_cast<UIManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg)
    {
    case WM_SIZE:
    {
        if (self && self->device_ != nullptr && wp != SIZE_MINIMIZED)
        {
            self->CleanupRenderTarget();
            self->swapChain_->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            self->CreateRenderTarget();
        }

        return 0;
    }

    case WM_SYSCOMMAND:
    {
        if ((wp & 0xfff0) == SC_KEYMENU)
            return 0;

        break;
    }

    case WM_CLOSE:
    {
        if (self)
            self->running_ = false;

        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }

    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);

        if (hit != HTCLIENT)
            return hit;

        UIManager* self = reinterpret_cast<UIManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (!self)
            return HTCLIENT;

        POINT pt{
            GET_X_LPARAM(lp),
            GET_Y_LPARAM(lp)
        };

        ScreenToClient(hwnd, &pt);

        constexpr int titleBarHeight = 34;
        constexpr int buttonsWidth = 80;

        RECT rc;
        GetClientRect(hwnd, &rc);

        bool inTitleBar = pt.y >= 0 && pt.y < titleBarHeight;

        bool inButtons = pt.x >= rc.right - buttonsWidth;

        if (inTitleBar && !inButtons)
            return HTCAPTION;

        return HTCLIENT;
    }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}