#include "platform/GameFocus.h"

bool GameFocusSupported()
{
    return false;
}

GameFocus QueryGameFocus()
{
    return GameFocus::Unknown;
}
