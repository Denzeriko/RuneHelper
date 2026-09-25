#include "platform/UIBackend.h"

#include <d3d11.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>

#include <chrono>
#include <string>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "core/Logger.h"
#include "ui/ImGuiStyleSetup.h"
#include "ui/UIDraw.h"
#include "ui/UIManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
bool IsMouseVk(int vk)
{
    switch (vk)
    {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2: return true;
    default: return false;
    }
}

constexpr int kToggleOcrHotkeyId = 1;
constexpr int kSingleSnapshotHotkeyId = 2;
constexpr int kSelectRegionHotkeyId = 3;

constexpr int kUnfocusedFrameIntervalMs = 100;
constexpr float kDefaultDpi = 96.0f;
constexpr int kWindowX = 100;
constexpr int kWindowY = 100;

int ScaledPixels(int pixels, float scale)
{
    return static_cast<int>(static_cast<float>(pixels) * scale + 0.5f);
}

std::string VkToString(int vk)
{
    if (vk == 0)
        return "None";

    UINT scan = MapVirtualKey(vk, MAPVK_VK_TO_VSC);

    char name[128]{};

    if (GetKeyNameTextA(scan << 16, name, sizeof(name)))
        return name;

    return std::to_string(vk);
}
}

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
    std::chrono::steady_clock::time_point lastFrame{};

    float dpiScale = 1.0f;
    float styledScale = 0.0f;

    bool IsInteractive() const;
    void ApplyScale();

    bool CreateWindowUI();
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void RegisterHotkey(int id, int key, const char* label);
    void RequestFromHotkey(int id);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

bool UIBackend::Impl::IsInteractive() const
{
    if (!hwnd)
        return false;

    if (GetForegroundWindow() == hwnd)
        return true;

    POINT cursor{};

    if (!GetCursorPos(&cursor))
        return false;

    RECT bounds{};

    if (!GetWindowRect(hwnd, &bounds))
        return false;

    return PtInRect(&bounds, cursor) != FALSE;
}

UIBackend::UIBackend() : impl_(std::make_unique<Impl>()) {}

UIBackend::~UIBackend()
{
    Shutdown();
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

    ImGuiStyleSetup::AddFonts();
    impl_->ApplyScale();

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
    LOG_INFO("Windows UI backend initialized, display scale " + std::to_string(ScaledPixels(100, impl_->dpiScale)) + "%");
    return true;
}

void UIBackend::Shutdown()
{
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
    if (!impl_->running)
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

    if (IsIconic(impl_->hwnd))
        return false;

    const auto now = std::chrono::steady_clock::now();

    if (!impl_->IsInteractive() && now - impl_->lastFrame < std::chrono::milliseconds(kUnfocusedFrameIntervalMs))
        return false;

    impl_->lastFrame = now;

    if (impl_->styledScale != impl_->dpiScale)
        impl_->ApplyScale();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void UIBackend::EndFrame()
{
    if (!impl_->deviceContext || !impl_->swapChain || !impl_->renderTargetView)
        return;

    ImGui::Render();

    const float clearColor[4] = { 0.08f, 0.08f, 0.08f, 1.0f };
    impl_->deviceContext->OMSetRenderTargets(1, &impl_->renderTargetView, nullptr);
    impl_->deviceContext->ClearRenderTargetView(impl_->renderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    impl_->swapChain->Present(0, 0);
}

bool UIBackend::IsRunning() const
{
    return impl_->running;
}

void UIBackend::Minimize()
{
    if (impl_->hwnd)
        ShowWindow(impl_->hwnd, SW_MINIMIZE);
}

void UIBackend::RequestClose()
{
    impl_->running = false;

    if (impl_->hwnd)
        PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
}

std::string UIBackend::HotkeyToString(int key) const
{
    if (key == 0)
        return "None";

    return VkToString(key);
}

bool UIBackend::CaptureNextHotkey(int& key)
{
    if (GetAsyncKeyState(VK_ESCAPE) & 1)
    {
        key = 0;
        return true;
    }

    for (int vk = 1; vk < 256; ++vk)
    {
        if (IsMouseVk(vk))
            continue;

        if (GetAsyncKeyState(vk) & 1)
        {
            key = vk;
            return true;
        }
    }

    return false;
}

void UIBackend::RegisterHotkeys(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey)
{
    if (!impl_->hwnd)
        return;

    UnregisterHotkeys();

    impl_->RegisterHotkey(kToggleOcrHotkeyId, toggleOcrKey, "toggle OCR");
    impl_->RegisterHotkey(kSingleSnapshotHotkeyId, singleSnapshotKey, "single snapshot");
    impl_->RegisterHotkey(kSelectRegionHotkeyId, selectRegionKey, "select region");
    impl_->hotkeysRegistered = true;
}

void UIBackend::UnregisterHotkeys()
{
    if (!impl_->hwnd || !impl_->hotkeysRegistered)
        return;

    UnregisterHotKey(impl_->hwnd, kToggleOcrHotkeyId);
    UnregisterHotKey(impl_->hwnd, kSingleSnapshotHotkeyId);
    UnregisterHotKey(impl_->hwnd, kSelectRegionHotkeyId);
    impl_->hotkeysRegistered = false;
}

void UIBackend::Impl::ApplyScale()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();

    ImGui::StyleColorsDark();
    ImGuiStyleSetup::ApplyRuneHelperStyle();

    if (dpiScale != 1.0f)
        style.ScaleAllSizes(dpiScale);

#if IMGUI_VERSION_NUM >= 19200
    style.FontScaleDpi = dpiScale;
#else
    ImGui::GetIO().FontGlobalScale = dpiScale;
#endif

    styledScale = dpiScale;
}

bool UIBackend::Impl::CreateWindowUI()
{
    windowClass = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                    L"RuneHelperConfig", nullptr };

    RegisterClassExW(&windowClass);

    dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ kWindowX, kWindowY }, MONITOR_DEFAULTTOPRIMARY));

    hwnd = CreateWindowW(
        windowClass.lpszClassName,
        L"RuneHelper",
        WS_POPUP,
        kWindowX,
        kWindowY,
        ScaledPixels(UIDraw::kWindowWidth, dpiScale),
        ScaledPixels(UIDraw::kWindowHeight, dpiScale),
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
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

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

void UIBackend::Impl::RequestFromHotkey(int id)
{
    UIRequests& requests = manager->State().requests;

    if (id == kToggleOcrHotkeyId)
        requests.toggleOcr = true;
    else if (id == kSingleSnapshotHotkeyId)
        requests.singleSnapshot = true;
    else if (id == kSelectRegionHotkeyId)
        requests.selectRegion = true;
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
        if (self)
            self->RequestFromHotkey(static_cast<int>(wp));
        return 0;

    case WM_SIZE:
        if (self && self->device && wp != SIZE_MINIMIZED)
        {
            self->CleanupRenderTarget();
            self->swapChain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lp)), static_cast<UINT>(HIWORD(lp)), DXGI_FORMAT_UNKNOWN, 0);
            self->CreateRenderTarget();
        }
        return 0;

    case WM_DPICHANGED:
        if (self)
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);

            self->dpiScale = static_cast<float>(LOWORD(wp)) / kDefaultDpi;

            SetWindowPos(
                hwnd,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
        }
        return 0;

    case WM_DESTROY:
        if (self)
            self->running = false;
        PostQuitMessage(0);
        return 0;

    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);

        if (hit != HTCLIENT)
            return hit;

        if (!self || !self->manager)
            return HTCLIENT;

        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        ScreenToClient(hwnd, &pt);

        const UIState& state = self->manager->State();
        const bool inTitleBar = pt.y >= 0 && static_cast<float>(pt.y) < state.titleBarBottom;
        const bool inButtons = static_cast<float>(pt.x) >= state.titleButtonsLeft;

        if (inTitleBar && !inButtons)
            return HTCAPTION;

        return HTCLIENT;
    }
    default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
