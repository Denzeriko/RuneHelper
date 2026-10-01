#include "platform/ClipboardWatcher.h"

#include <windows.h>

#include <chrono>

struct ClipboardWatcher::Impl
{
    HWND window = nullptr;
    bool pending = false;
    std::chrono::steady_clock::time_point deadline{};

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (self && message == WM_CLIPBOARDUPDATE)
        {
            self->pending = true;
            self->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
};

ClipboardWatcher::ClipboardWatcher() : impl_(std::make_unique<Impl>()) {}

ClipboardWatcher::~ClipboardWatcher()
{
    Stop();
}

bool ClipboardWatcher::Start()
{
    if (Running())
        return true;

    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc = Impl::WindowProc;
    cls.hInstance = instance;
    cls.lpszClassName = L"RuneHelperClipboard";

    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    impl_->window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, impl_.get());

    if (!impl_->window || !AddClipboardFormatListener(impl_->window))
    {
        Stop();
        return false;
    }

    return true;
}

void ClipboardWatcher::Stop()
{
    if (impl_->window)
    {
        RemoveClipboardFormatListener(impl_->window);
        DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }

    impl_->pending = false;
}

bool ClipboardWatcher::Running() const
{
    return impl_->window != nullptr;
}

std::optional<std::string> ClipboardWatcher::Poll()
{
    if (!Running())
        return std::nullopt;

    MSG message{};

    while (PeekMessageW(&message, impl_->window, 0, 0, PM_REMOVE))
        DispatchMessageW(&message);

    if (!impl_->pending)
        return std::nullopt;

    if (!OpenClipboard(impl_->window))
    {
        if (std::chrono::steady_clock::now() < impl_->deadline)
            return std::nullopt;

        impl_->pending = false;
        return std::string{};
    }

    impl_->pending = false;
    std::string result;
    const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    const SIZE_T bytes = handle ? GlobalSize(handle) : 0;

    if (bytes >= sizeof(wchar_t) && bytes <= (kMaxClipboardText + 1) * sizeof(wchar_t))
    {
        if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle)))
        {
            std::size_t length = 0;

            while (length < bytes / sizeof(wchar_t) && data[length] != L'\0')
                ++length;

            if (length < bytes / sizeof(wchar_t) && length > 0)
            {
                const int size =
                    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0, nullptr, nullptr);

                if (size > 0 && static_cast<std::size_t>(size) <= kMaxClipboardText)
                {
                    result.resize(size);
                    WideCharToMultiByte(
                        CP_UTF8,
                        WC_ERR_INVALID_CHARS,
                        data,
                        static_cast<int>(length),
                        result.data(),
                        size,
                        nullptr,
                        nullptr
                    );
                }
            }

            GlobalUnlock(handle);
        }
    }

    CloseClipboard();
    return result;
}
