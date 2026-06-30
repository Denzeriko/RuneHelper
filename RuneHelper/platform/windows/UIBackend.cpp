#include "platform/UIBackend.h"

#include <d3d11.h>
#include <tchar.h>
#include <windows.h>

#include <string>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "core/Logger.h"
#include "ui/UIManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct UIBackend::Impl
{
    HWND hwnd = nullptr;
    WNDCLASSEXW windowClass = {};

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;

    UIManager* manager = nullptr;
    bool running = false;
    bool hotkeysRegistered = false;

    bool CreateWindowUI();
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void RegisterHotkey(int id, int key, const char* label);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

UIBackend::UIBackend()
    : impl_(new Impl())
{
}

UIBackend::~UIBackend()
{
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool UIBackend::Init(UIManager* manager)
{
    impl_->manager = manager;

    if (!impl_->CreateWindowUI())
        return false;

    if (!impl_->CreateDeviceD3D())
    {
        impl_->CleanupDeviceD3D();
        DestroyWindow(impl_->hwnd);
        UnregisterClassW(impl_->windowClass.lpszClassName, impl_->windowClass.hInstance);
        impl_->hwnd = nullptr;
        return false;
    }

    ShowWindow(impl_->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(impl_->hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    SetupStyle();

    if (!ImGui_ImplWin32_Init(impl_->hwnd))
    {
        LOG_ERROR("Windows UI: ImGui Win32 backend init failed");
        Shutdown();
        return false;
    }

    if (!ImGui_ImplDX11_Init(impl_->device, impl_->deviceContext))
    {
        LOG_ERROR("Windows UI: ImGui DX11 backend init failed");
        Shutdown();
        return false;
    }

    impl_->running = true;
    LOG_INFO("Windows UI backend initialized");
    return true;
}

void UIBackend::SetupStyle()
{
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
}

void UIBackend::Shutdown()
{
    if (!impl_)
        return;

    impl_->running = false;
    UnregisterHotkeys();

    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    impl_->CleanupDeviceD3D();

    if (impl_->hwnd)
    {
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }

    if (impl_->windowClass.lpszClassName)
    {
        UnregisterClassW(impl_->windowClass.lpszClassName, impl_->windowClass.hInstance);
        impl_->windowClass = {};
    }
}

bool UIBackend::BeginFrame()
{
    if (!impl_ || !impl_->running)
        return false;

    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
            impl_->running = false;
    }

    if (!impl_->running)
        return false;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void UIBackend::EndFrame()
{
    if (!impl_ || !impl_->deviceContext || !impl_->swapChain || !impl_->renderTargetView)
        return;

    ImGui::Render();

    const float clearColor[4] = {0.08f, 0.08f, 0.08f, 1.0f};
    impl_->deviceContext->OMSetRenderTargets(1, &impl_->renderTargetView, nullptr);
    impl_->deviceContext->ClearRenderTargetView(impl_->renderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    impl_->swapChain->Present(1, 0);
}

bool UIBackend::IsRunning() const
{
    return impl_ && impl_->running;
}

void UIBackend::RequestClose()
{
    if (!impl_)
        return;

    impl_->running = false;

    if (impl_->hwnd)
        PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
}

void UIBackend::RegisterHotkeys(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey)
{
    if (!impl_ || !impl_->hwnd)
        return;

    UnregisterHotkeys();

    impl_->RegisterHotkey(1, toggleOcrKey, "toggle OCR");
    impl_->RegisterHotkey(2, singleSnapshotKey, "single snapshot");
    impl_->RegisterHotkey(3, selectRegionKey, "select region");
    impl_->hotkeysRegistered = true;
}

void UIBackend::UnregisterHotkeys()
{
    if (!impl_ || !impl_->hwnd || !impl_->hotkeysRegistered)
        return;

    UnregisterHotKey(impl_->hwnd, 1);
    UnregisterHotKey(impl_->hwnd, 2);
    UnregisterHotKey(impl_->hwnd, 3);
    impl_->hotkeysRegistered = false;
}

bool UIBackend::Impl::CreateWindowUI()
{
    windowClass = {
        sizeof(WNDCLASSEXW),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        GetModuleHandle(nullptr),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"RuneHelperConfig",
        nullptr
    };

    RegisterClassExW(&windowClass);

    hwnd = CreateWindowW(
        windowClass.lpszClassName,
        L"RuneHelper",
        WS_POPUP,
        100,
        100,
        420,
        680,
        nullptr,
        nullptr,
        windowClass.hInstance,
        this
    );

    if (!hwnd)
    {
        LOG_ERROR("Windows UI: CreateWindow failed");
        return false;
    }

    return true;
}

bool UIBackend::Impl::CreateDeviceD3D()
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &swapChain,
        &device,
        &featureLevel,
        &deviceContext
    );

    if (FAILED(result))
    {
        LOG_ERROR("Windows UI: D3D11CreateDeviceAndSwapChain failed");
        return false;
    }

    CreateRenderTarget();
    return true;
}

void UIBackend::Impl::CleanupDeviceD3D()
{
    CleanupRenderTarget();

    if (swapChain)
    {
        swapChain->Release();
        swapChain = nullptr;
    }

    if (deviceContext)
    {
        deviceContext->Release();
        deviceContext = nullptr;
    }

    if (device)
    {
        device->Release();
        device = nullptr;
    }
}

void UIBackend::Impl::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));

    if (!backBuffer)
        return;

    device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
    backBuffer->Release();
}

void UIBackend::Impl::CleanupRenderTarget()
{
    if (renderTargetView)
    {
        renderTargetView->Release();
        renderTargetView = nullptr;
    }
}

void UIBackend::Impl::RegisterHotkey(int id, int key, const char* label)
{
    if (key == 0)
        return;

    if (!RegisterHotKey(hwnd, id, MOD_NOREPEAT, static_cast<UINT>(key)))
        LOG_ERROR(std::string("Windows UI: failed to register hotkey for ") + label);
}

LRESULT CALLBACK UIBackend::Impl::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;

    Impl* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Impl*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg)
    {
    case WM_HOTKEY:
        if (self && self->manager)
        {
            if (wp == 1)
                self->manager->RequestToggleOCR();
            else if (wp == 2)
                self->manager->RequestSingleSnapshot();
            else if (wp == 3)
                self->manager->RequestSelectRegion();
        }
        return 0;

    case WM_SIZE:
        if (self && self->device && wp != SIZE_MINIMIZED)
        {
            self->CleanupRenderTarget();
            self->swapChain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lp)), static_cast<UINT>(HIWORD(lp)), DXGI_FORMAT_UNKNOWN, 0);
            self->CreateRenderTarget();
        }
        return 0;

    case WM_DESTROY:
        if (self)
            self->running = false;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
