find_package(OpenCV CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

set(RUNEHELPER_LIBRARIES
    ${OpenCV_LIBS}
    Tesseract::libtesseract
    cpr::cpr
    nlohmann_json::nlohmann_json
    imgui::imgui
    d3d11
    dxgi
    dwmapi
    psapi
)

set(RUNEHELPER_DEFINITIONS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)