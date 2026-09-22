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

project(RuneHelper VERSION 1.7.5 LANGUAGES ${RUNEHELPER_LANGUAGES})

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

option(RUNEHELPER_BUILD_TESTS "Build the OCR golden-file test" OFF)
option(RUNEHELPER_WERROR "Treat warnings in RuneHelper sources as errors" OFF)
option(RUNEHELPER_LTO "Build Release with link time optimisation" OFF)

set(RUNEHELPER_LINUX_BACKEND "x11" CACHE STRING "Linux display backend: x11 or wayland")
set_property(CACHE RUNEHELPER_LINUX_BACKEND PROPERTY STRINGS x11 wayland)

if(UNIX AND NOT APPLE AND NOT RUNEHELPER_LINUX_BACKEND MATCHES "^(x11|wayland)$")
    message(FATAL_ERROR "RUNEHELPER_LINUX_BACKEND must be x11 or wayland, got '${RUNEHELPER_LINUX_BACKEND}'")
endif()

set(RUNEHELPER_COMMIT "" CACHE STRING "Commit hash shown next to the version in the UI")

if(NOT RUNEHELPER_COMMIT)
    include(GitCommit)
    runehelper_read_commit(RUNEHELPER_COMMIT)
endif()

if(NOT RUNEHELPER_COMMIT)
    find_package(Git QUIET)

    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE RUNEHELPER_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
endif()

string(TOLOWER "${RUNEHELPER_COMMIT}" RUNEHELPER_COMMIT)
string(REGEX REPLACE "[^0-9a-f]" "" RUNEHELPER_COMMIT "${RUNEHELPER_COMMIT}")
string(LENGTH "${RUNEHELPER_COMMIT}" RUNEHELPER_COMMIT_LENGTH)

if(RUNEHELPER_COMMIT_LENGTH GREATER 5)
    string(SUBSTRING "${RUNEHELPER_COMMIT}" 0 5 RUNEHELPER_COMMIT)
endif()

if(RUNEHELPER_COMMIT)
    set(RUNEHELPER_VERSION_LABEL "${PROJECT_VERSION} (${RUNEHELPER_COMMIT})")
else()
    set(RUNEHELPER_VERSION_LABEL "${PROJECT_VERSION}")
endif()

message(STATUS "RuneHelper version label: ${RUNEHELPER_VERSION_LABEL}")

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
    RUNEHELPER_VERSION_LABEL="${RUNEHELPER_VERSION_LABEL}"
    RUNEHELPER_COMMIT="${RUNEHELPER_COMMIT}"
    ${RUNEHELPER_DEFINITIONS}
)

target_link_libraries(RuneHelper PRIVATE
    ${RUNEHELPER_LIBRARIES}
)

include(CompilerOptions)

if(RUNEHELPER_BUILD_TESTS)
    if(WIN32)
        message(FATAL_ERROR "RUNEHELPER_BUILD_TESTS is only wired up for Linux so far")
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()

set_target_properties(RuneHelper PROPERTIES
    OUTPUT_NAME "RuneHelper"
)

if(WIN32)
    set_target_properties(RuneHelper PROPERTIES
        WIN32_EXECUTABLE ON
    )
endif()
