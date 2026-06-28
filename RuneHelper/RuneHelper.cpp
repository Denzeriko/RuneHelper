#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "core/RuneHelperApp.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    RuneHelperApp app;
    return app.Run();
}