#pragma once

#include <exception>
#include <string>
#include <utility>

#include "core/Logger.h"

template <typename Body>
bool RunLoggingExceptions(const char* context, Body&& body)
{
    try
    {
        std::forward<Body>(body)();
        return true;
    }
    catch (const std::exception& error)
    {
        LOG_ERROR(std::string(context) + ": unhandled exception: " + error.what());
    }
    catch (...)
    {
        LOG_ERROR(std::string(context) + ": unhandled exception of unknown type");
    }

    return false;
}
