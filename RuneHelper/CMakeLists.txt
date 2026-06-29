cmake_minimum_required(VERSION 3.20)

if(WIN32)
  message(FATAL_ERROR "This CMake build is Linux-only. Use Visual Studio on Windows.")
endif()

set(OpenGL_GL_PREFERENCE GLVND)

include(FetchContent)

project(RuneHelper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV QUIET)
find_package(Tesseract QUIET)
# Ubuntu does not bundle the cmake config files for Tesseract, so we use pkg-config as a fallback
# https://stackoverflow.com/questions/65970070/use-tesseract-in-cmake-project
if (NOT Tesseract_FOUND)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(Tesseract REQUIRED IMPORTED_TARGET tesseract lept)
    add_library(Tesseract::libtesseract ALIAS PkgConfig::Tesseract)
endif ()
find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)
find_package(glfw3 QUIET)

if(NOT glfw3_FOUND)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
  )

  FetchContent_MakeAvailable(glfw)
endif()

FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG        1.14.2
)

set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cpr)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.90.9
)

FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC
  glfw
  OpenGL::GL
)

set(RUNEHELPER_MISSING_PACKAGES)

if(NOT OpenCV_FOUND)
  list(APPEND RUNEHELPER_MISSING_PACKAGES
    "  - OpenCV: install libopencv-dev")
endif()

if(NOT Tesseract_FOUND)
  list(APPEND RUNEHELPER_MISSING_PACKAGES
    "  - Tesseract: install libtesseract-dev")
endif()


if(RUNEHELPER_MISSING_PACKAGES)
  list(JOIN RUNEHELPER_MISSING_PACKAGES "\n" RUNEHELPER_MISSING_PACKAGES_TEXT)
  message(FATAL_ERROR
    "Missing Linux build dependencies:\n"
    "${RUNEHELPER_MISSING_PACKAGES_TEXT}\n\n"
    "On Ubuntu, install them with:\n"
    "  sudo apt update\n"
    "  sudo apt install build-essential cmake pkg-config libopencv-dev libtesseract-dev libleptonica-dev libx11-dev"
  )
endif()

add_executable(runehelper
  RuneHelper.cpp
  core/RuneHelperApp.cpp
  core/ConfigManager.cpp
  core/Logger.cpp
  core/Helpers.cpp
  core/UpdateChecker.cpp
  ocr/LootParser.cpp
  ocr/NameNormalizer.cpp
  ocr/OCR.cpp
  price/PriceCache.cpp
  ui/Overlay.cpp
  platform/linux/UIManager.cpp
  platform/linux/ScreenCapture.cpp
  platform/linux/RegionSelect.cpp
  platform/linux/ResourceHelper.cpp
)

target_include_directories(runehelper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS}
  ${X11_INCLUDE_DIR}
)

target_link_libraries(runehelper PRIVATE
  ${OpenCV_LIBS}
  Tesseract::libtesseract
  cpr::cpr
  imgui
  glfw
  OpenGL::GL
  ${X11_LIBRARIES}
)
