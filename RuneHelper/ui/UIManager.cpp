#include "UIManager.h"

#include <iostream>

#include <windowsx.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "core/Version.h"
#include "core/Helpers.h"
#include "ui/UIState.h"

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

    state_.running = true;

    return true;
}

void UIManager::Shutdown()
{
    if (!state_.running && !hwnd_)
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

    state_.running = false;
}

void UIManager::SetStatus(bool ocrInitializing, bool ocrReady, bool ocrFailed)
{
    state_.ocrInitializing = ocrInitializing;
    state_.ocrReady = ocrReady;
    state_.ocrFailed = ocrFailed;
}

void UIManager::SetUpdateChecker(UpdateChecker* checker)
{
    updateChecker_ = checker;
}

bool UIManager::IsRunning() const
{
    return state_.running;
}

bool UIManager::WantsSelectRegion()
{
    bool value = state_.wantsSelectRegion;
    state_.wantsSelectRegion = false;
    return value;
}

bool UIManager::WantsRefreshPrices()
{
    bool value = state_.wantsRefreshPrices;
    state_.wantsRefreshPrices = false;
    return value;
}

bool UIManager::IsRegionHovered() const
{
    return state_.regionHovered;
}

void UIManager::SetPriceStatus(bool downloading, size_t priceCount)
{
    state_.priceDownloading = downloading;
    state_.priceCount = priceCount;
}

void UIManager::RegisterHotkeys()
{
    UnregisterHotkeys();
    RegisterHotKey(hwnd_, 1, MOD_NOREPEAT, config_->hotkeyToggleOCR);
    RegisterHotKey(hwnd_, 2, MOD_NOREPEAT, config_->hotkeySingleSnapshot);
    RegisterHotKey(hwnd_, 3, MOD_NOREPEAT, config_->hotkeySelectRegion);
}

void UIManager::UnregisterHotkeys()
{
    UnregisterHotKey(hwnd_, 1);
    UnregisterHotKey(hwnd_, 2);
    UnregisterHotKey(hwnd_, 3);
}

void UIManager::SetDebugData(const DebugData& data)
{
    debugData_ = std::move(data);
}

bool UIManager::WantsToggleOCR()
{
    bool v = state_.wantsToggleOCR;
    state_.wantsToggleOCR = false;
    return v;
}

bool UIManager::WantsSingleSnapshot()
{
    bool v = state_.wantsSingleSnapshot;
    state_.wantsSingleSnapshot = false;
    return v;
}

void UIManager::Pump()
{
    if (!state_.running)
        return;

    MSG msg;

    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
            state_.running = false;
    }

    if (!state_.running)
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
        665,
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
    ImGui::TextDisabled("v%s", RUNEHELPER_VERSION);

    ImGui::SameLine(ImGui::GetWindowWidth() - 40.0f);

    if (ImGui::Button("_", ImVec2(16, 16)))
        ShowWindow(hwnd_, SW_MINIMIZE);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));

    if (ImGui::Button("X", ImVec2(16, 16)))
    {
        state_.running = false;
        DestroyWindow(hwnd_);
    }
    ImGui::PopStyleColor(3);

    ImGui::EndChild();
}

void UIManager::DrawMainTab()
{
    DrawStatusSection();
    DrawRegionSection();
    DrawOcrSection();
    DrawHotkeysSection();
    DrawOverlaySection();
    DrawPricesSection();
    DrawBottomSection();
}

namespace
{
    constexpr ImVec4 kGreen {0.5f, 1.0f, 0.5f, 1.0f};
    constexpr ImVec4 kYellow{1.0f, 0.8f, 0.2f, 1.0f};
    constexpr ImVec4 kRed   {1.0f, 0.3f, 0.3f, 1.0f};
}

void UIManager::DrawDebugTab()
{
    ImGui::SeparatorText("OCR DEBUG");

    if (debugData_.lines.empty())
    {
        ImGui::TextDisabled("No OCR data yet.");
        return;
    }

    if (ImGui::BeginTable("ocr_debug_table", 4,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("OCR Text");
        ImGui::TableSetupColumn("Matched");
        ImGui::TableSetupColumn("Confidence");
        ImGui::TableSetupColumn("Price");

        ImGui::TableHeadersRow();

        for (const auto& line : debugData_.lines)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", line.ocrText.c_str());

            ImGui::TableSetColumnIndex(1);

            if (line.matchedText == "-")
                ImGui::TextDisabled("-");
            else
                ImGui::TextWrapped("%s", line.matchedText.c_str());

            ImGui::TableSetColumnIndex(2);

            if (line.confidence >= 90)
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%d%%", line.confidence);
            else if (line.confidence >= 75)
                ImGui::TextColored( ImVec4(1.0f, 0.8f, 0.2f, 1.0f),"%d%%", line.confidence);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%d%%", line.confidence);

            ImGui::TableSetColumnIndex(3);

            if (line.price == "-")
                ImGui::TextDisabled("-");
            else
                ImGui::Text("%s", line.price.c_str());
        }

        ImGui::EndTable();
    }
}

void UIManager::DrawStatusSection()
{
    ImGui::SeparatorText("STATUS");
    if (ImGui::BeginTable("status_table", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("OCR");

        ImGui::TableSetColumnIndex(1);

        if (state_.ocrInitializing)
            ImGui::TextColored(kYellow, "Initializing...");
        else if (state_.ocrFailed)
            ImGui::TextColored(kRed, "Failed");
        else if (state_.ocrReady)
            ImGui::TextColored(kGreen, "Ready");
        else
            ImGui::TextDisabled("Waiting...");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Prices");

        ImGui::TableSetColumnIndex(1);

        if (state_.priceDownloading)
            ImGui::TextColored(kYellow, "Downloading...");
        else
            ImGui::TextColored(kGreen, "%zu items loaded", state_.priceCount);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Version");

        ImGui::TableSetColumnIndex(1);

        ImGui::Text("v%s", RUNEHELPER_VERSION);

        if (updateChecker_)
        {
            if (updateChecker_->IsChecking())
            {
                ImGui::SameLine();
                ImGui::TextColored(kYellow, "(checking...)");
            }
            else if (updateChecker_->HasUpdate())
            {
                ImGui::SameLine();
                ImGui::TextColored(kGreen, "(update available)");

                ImGui::SameLine();

                if (ImGui::SmallButton("Download"))
                {
                    ShellExecuteA(
                        nullptr,
                        "open",
                        updateChecker_->DownloadUrl().c_str(),
                        nullptr,
                        nullptr,
                        SW_SHOWNORMAL);
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
}

void UIManager::DrawRegionSection()
{
    ImGui::SeparatorText("REGION");
    if (ImGui::Button("Select Region"))
        state_.wantsSelectRegion = true;

    state_.regionHovered = ImGui::IsItemHovered();

    ImGui::SameLine();
    if (config_->regionW > 0)
        ImGui::TextDisabled("x:%d y:%d w:%d h:%d", config_->regionX, config_->regionY, config_->regionW, config_->regionH);
    else
        ImGui::TextColored(kYellow, "No region selected");

    ImGui::Spacing();
}

void UIManager::DrawOcrSection()
{
    ImGui::SeparatorText("OCR");
    ImGui::Checkbox("Enable OCR", &config_->ocrEnabled);
    ImGui::SameLine();

    if (config_->ocrEnabled)
        ImGui::TextColored(kGreen, "Running");
    else
        ImGui::TextColored(kRed, "Stopped");

    ImGui::Checkbox("OCR AutoDetect Menu (Experimental)", &config_->ocrAutoDetect);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Only shows the overlay when the Runeshape menu is detected.\nMay occasionally fail due to OCR inaccuracies.");

    ImGui::SliderInt("OCR passes", &config_->ocrPasses, 1, 6);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("More passes = better OCR recognize, but higher CPU usage");
    /*
    ImGui::SliderFloat("OCR Threshold", reinterpret_cast<float*>(&config_->ocrThreshold), 0.0f, 255.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Image binarization threshold.\nLower values keep more details.\nHigher values remove noise but may lose characters.");
    */

    ImGui::SliderInt("OCR interval (ms)", &config_->ocrIntervalMs, 100, 2000);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lower values = faster updates but higher CPU usage.");

    ImGui::Spacing();
}

void UIManager::DrawHotkeysSection()
{
    ImGui::SeparatorText("HOTKEYS");
    DrawHotkeyButton("Toggle OCR", config_->hotkeyToggleOCR);
    DrawHotkeyButton("Single Snapshot", config_->hotkeySingleSnapshot);
    DrawHotkeyButton("Select Region", config_->hotkeySelectRegion);
    ImGui::Spacing();
}

void UIManager::DrawOverlaySection()
{
    ImGui::SeparatorText("OVERLAY");
    ImGui::SliderInt("Offset X", &config_->overlayOffsetX, -300, 500);
    ImGui::SliderInt("Offset Y", &config_->overlayOffsetY, -200, 200);
    ImGui::SliderInt("Font Size", &config_->overlayFontSize, 8, 48);
    ImGui::Spacing();
}

void UIManager::DrawPricesSection()
{
    ImGui::SeparatorText("PRICES");
    ImGui::InputInt("Green >= ex", &config_->priceColorMedium);
    ImGui::InputInt("Yellow >= ex", &config_->priceColorHigh);
    ImGui::InputInt("Red >= ex", &config_->priceColorVeryHigh);
    ImGui::SliderInt("Refresh minutes", &config_->priceRefreshMinutes, 1, 60);

    if (ImGui::Button("Refresh Prices"))
        state_.wantsRefreshPrices = true;

    ImGui::Spacing();
}

void UIManager::DrawBottomSection()
{
    ImGui::Separator();

    if (ImGui::Button("Save"))
    {
        if (configManager_)
        {
            configManager_->Save();
            state_.showSaved = true;
            state_.savedAt = std::chrono::steady_clock::now();
        }
    }

    if (state_.showSaved)
    {
        auto elapsed = std::chrono::steady_clock::now() - state_.savedAt;

        if (elapsed < std::chrono::seconds(1))
        {
            ImGui::SameLine();
            ImGui::TextColored(kGreen, "Saved!");
        }
        else
            state_.showSaved = false;
    }

    const char* DenzTag = "Denz";
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -ImGui::CalcTextSize(DenzTag).x);
    ImGui::TextDisabled("%s", DenzTag);
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

    DrawTitleBar();

    if (ImGui::BeginTabBar("MainTabs"))
    {
        if (ImGui::BeginTabItem("RuneHelper"))
        {
            DrawMainTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Debug Menu"))
        {
            DrawDebugTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void UIManager::DrawHotkeyButton(const char* label, int& key)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(220);

    std::string text;

    if (waitingForHotkey_ == &key)
        text = "Press any key...";
    else
        text = VkToString(key);

    if (ImGui::Button(text.c_str(), ImVec2(180, 0)))
    {
        waitingForHotkey_ = &key;
        hotkeyCaptureSkipFrame_ = true;
    }

    if (waitingForHotkey_ != &key)
        return;

    if (hotkeyCaptureSkipFrame_)
    {
        hotkeyCaptureSkipFrame_ = false;
        return;
    }

    if (GetAsyncKeyState(VK_ESCAPE) & 1)
    {
        key = 0;

        waitingForHotkey_ = nullptr;

        if (configManager_)
            configManager_->Save();

        RegisterHotkeys();

        return;
    }

    for (int vk = 1; vk < 256; ++vk)
    {
        if (IsMouseVk(vk))
            continue;

        if (GetAsyncKeyState(vk) & 1)
        {
            key = vk;

            waitingForHotkey_ = nullptr;

            if (configManager_)
                configManager_->Save();

            RegisterHotkeys();

            break;
        }
    }
}

bool UIManager::IsMouseVk(int vk)
{
    switch (vk)
    {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    }

    return false;
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
            self->state_.running = false;

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
    case WM_HOTKEY:
    {
        switch (wp)
        {
        case 1:
            self->state_.wantsToggleOCR = true;
            break;

        case 2:
            self->state_.wantsSingleSnapshot = true;
            break;

        case 3:
            self->state_.wantsSelectRegion = true;
            break;
        }

        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}