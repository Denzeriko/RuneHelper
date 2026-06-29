cmake_minimum_required(VERSION 3.20)

if(WIN32)
  message(FATAL_ERROR "This CMake build is Linux-only. Use Visual Studio on Windows.")
endif()

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

FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG        1.14.2
)

set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cpr)

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
    "  sudo apt install build-essential cmake pkg-config libopencv-dev libtesseract-dev libleptonica-dev libcpr-dev"
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
  ui/UIManager.cpp
  platform/linux/ScreenCapture.cpp
  platform/linux/RegionSelect.cpp
  platform/linux/ResourceHelper.cpp
)

target_include_directories(runehelper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(runehelper PRIVATE
  ${OpenCV_LIBS}
  Tesseract::libtesseract
  cpr::cpr
)
