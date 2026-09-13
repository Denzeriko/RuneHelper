cmake_minimum_required(VERSION 3.15.0)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(RUNEHELPER_LANGUAGES CXX)

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    list(APPEND RUNEHELPER_LANGUAGES RC)
endif()

project(RuneHelper VERSION 1.6.0 LANGUAGES ${RUNEHELPER_LANGUAGES})

get_property(RUNEHELPER_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(NOT RUNEHELPER_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

set(RUNEHELPER_LINUX_BACKEND "x11" CACHE STRING "Linux display backend: x11 or wayland")
set_property(CACHE RUNEHELPER_LINUX_BACKEND PROPERTY STRINGS x11 wayland)

if(UNIX AND NOT APPLE AND NOT RUNEHELPER_LINUX_BACKEND MATCHES "^(x11|wayland)$")
    message(FATAL_ERROR "RUNEHELPER_LINUX_BACKEND must be x11 or wayland, got '${RUNEHELPER_LINUX_BACKEND}'")
endif()

include(Sources)

if(WIN32)
    include(DependenciesWindows)
elseif(UNIX AND NOT APPLE)
    include(DependenciesLinux)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

add_executable(RuneHelper
    ${RUNEHELPER_COMMON_SOURCES}
    ${RUNEHELPER_PLATFORM_SOURCES}
    ${RUNEHELPER_GENERATED_SOURCES}
)

target_include_directories(RuneHelper PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/RuneHelper"
    ${RUNEHELPER_INCLUDE_DIRECTORIES}
)

target_compile_definitions(RuneHelper PRIVATE
    RUNEHELPER_VERSION="${PROJECT_VERSION}"
    ${RUNEHELPER_DEFINITIONS}
)

target_link_libraries(RuneHelper PRIVATE
    ${RUNEHELPER_LIBRARIES}
)

include(CompilerOptions)

set_target_properties(RuneHelper PROPERTIES
    OUTPUT_NAME "RuneHelper"
)

if(WIN32)
    set_target_properties(RuneHelper PROPERTIES
        WIN32_EXECUTABLE ON
    )
endif()
