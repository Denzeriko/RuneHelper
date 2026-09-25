#pragma once

#include <cstdlib>
#include <string>

#include "core/Logger.h"
#include "core/Text.h"

inline bool IsWaylandSession()
{
    const char* type = std::getenv("XDG_SESSION_TYPE");

    return type && ToLowerAscii(type) == "wayland";
}

inline void LogWaylandBuildNeeded(const std::string& action)
{
    LOG_ERROR("The X11 build cannot " + action + " in a Wayland session, use the Wayland build of RuneHelper");
}
