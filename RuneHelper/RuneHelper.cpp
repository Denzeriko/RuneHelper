#ifdef _WIN32
#include <windows.h>
#endif

#include "core/RuneHelperApp.h"

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main()
#endif
{
    RuneHelperApp app;
    return app.Run();
}
