#ifdef _WIN32
#include <windows.h>
#else
#include "platform/linux/LinuxHotkeys.h"
#endif

#include "core/RuneHelperApp.h"

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    RuneHelperApp app;
    return app.Run();
}
#else
int main(int argc, char** argv)
{
    const int clientResult = RunLinuxHotkeyClient(argc, argv);

    if (clientResult >= 0)
        return clientResult;

    RuneHelperApp app;
    return app.Run();
}
#endif
