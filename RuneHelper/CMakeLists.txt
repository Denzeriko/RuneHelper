cmake_minimum_required(VERSION 3.20)

if(WIN32)
  message(FATAL_ERROR "This CMake build is Linux-only. Use Visual Studio on Windows.")
endif()

project(RuneHelper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV QUIET)
find_package(Tesseract QUIET)
find_package(cpr CONFIG QUIET)

set(RUNEHELPER_MISSING_PACKAGES)

if(NOT OpenCV_FOUND)
  list(APPEND RUNEHELPER_MISSING_PACKAGES "OpenCV")
endif()

if(NOT Tesseract_FOUND)
  list(APPEND RUNEHELPER_MISSING_PACKAGES "Tesseract")
endif()

if(NOT cpr_FOUND)
  list(APPEND RUNEHELPER_MISSING_PACKAGES "cpr")
endif()

if(RUNEHELPER_MISSING_PACKAGES)
  list(JOIN RUNEHELPER_MISSING_PACKAGES ", " RUNEHELPER_MISSING_PACKAGES_TEXT)
  message(FATAL_ERROR "Missing Linux build dependencies: ${RUNEHELPER_MISSING_PACKAGES_TEXT}")
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
