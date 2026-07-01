cmake_minimum_required(VERSION 3.15.0)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(RUNEHELPER_LANGUAGES CXX)

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    list(APPEND RUNEHELPER_LANGUAGES RC)
endif()

project(RuneHelper VERSION 1.3.1 LANGUAGES ${RUNEHELPER_LANGUAGES})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

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
)

target_include_directories(RuneHelper PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/RuneHelper"
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
