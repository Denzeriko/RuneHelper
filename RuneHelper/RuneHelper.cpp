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
    return RunCatching(0, nullptr);
}
#else
int main(int argc, char** argv)
{
    return RunCatching(argc, argv);
}
#endif
