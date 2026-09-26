#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>

#include "platform/linux/LinuxHotkeys.h"
#endif

#include <filesystem>
#include <optional>
#include <string_view>

#include "core/ExceptionLogging.h"
#include "core/Logger.h"
#include "core/RuneHelperApp.h"
#include "platform/PlatformShell.h"

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

int RunApp()
{
    std::optional<std::filesystem::path> restart;
    int result = 0;

    {
        RuneHelperApp app;
        result = app.Run();
        restart = app.RestartTarget();
    }

    if (restart && !StartProcess(*restart))
        LOG_ERROR("Update: the new version could not be started, start RuneHelper again");

    return result;
}

int RunClientOrApp(int argc, char** argv)
{
#ifndef _WIN32
    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        std::printf("RuneHelper %s, %s build\n", RUNEHELPER_VERSION_LABEL, RUNEHELPER_BUILD_VARIANT);
        return 0;
    }

    const int clientResult = RunLinuxHotkeyClient(argc, argv);

    if (clientResult >= 0)
        return clientResult;
#else
    (void)argc;
    (void)argv;
#endif

    return RunApp();
}

int Run(int argc, char** argv)
{
    int result = 1;
    RunLoggingExceptions("RuneHelper", [&] { result = RunClientOrApp(argc, argv); });
    return result;
}
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR commandLine, int)
{
    if (commandLine && std::string_view(commandLine) == "--version")
        return 0;

    UsePhysicalPixels();
    return Run(0, nullptr);
}
#else
int main(int argc, char** argv)
{
    return Run(argc, argv);
}
#endif
