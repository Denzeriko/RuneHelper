#ifdef _WIN32
#include <windows.h>
#else
#include "platform/linux/LinuxHotkeys.h"
#endif

#include <exception>
#include <string>

#include "core/Logger.h"
#include "core/RuneHelperApp.h"

namespace
{
#ifdef _WIN32
void UsePhysicalPixels()
{
    using SetAwarenessContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        const auto setContext =
            reinterpret_cast<SetAwarenessContext>(reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));

        if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;
    }

    SetProcessDPIAware();
}
#endif

int RunGuarded(int argc, char** argv)
{
#ifndef _WIN32
    const int clientResult = RunLinuxHotkeyClient(argc, argv);

    if (clientResult >= 0)
        return clientResult;
#else
    (void)argc;
    (void)argv;
#endif

    RuneHelperApp app;
    return app.Run();
}

int RunCatching(int argc, char** argv)
{
    try
    {
        return RunGuarded(argc, argv);
    }
    catch (const std::exception& error)
    {
        LOG_ERROR(std::string("Unhandled exception: ") + error.what());
    }
    catch (...)
    {
        LOG_ERROR("Unhandled exception of unknown type");
    }

    return 1;
}
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    UsePhysicalPixels();
    return RunCatching(0, nullptr);
}
#else
int main(int argc, char** argv)
{
    return RunCatching(argc, argv);
}
#endif
