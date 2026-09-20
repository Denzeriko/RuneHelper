set(RUNEHELPER_TESSDATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/tessdata")

add_custom_command(
    OUTPUT "${RUNEHELPER_TESSDATA_DIR}/eng.traineddata"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RUNEHELPER_TESSDATA_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/RuneHelper/resources/eng.traineddata_fast"
            "${RUNEHELPER_TESSDATA_DIR}/eng.traineddata"
    DEPENDS "${CMAKE_SOURCE_DIR}/RuneHelper/resources/eng.traineddata_fast"
    VERBATIM
)

add_custom_target(ocr_golden_tessdata
    DEPENDS "${RUNEHELPER_TESSDATA_DIR}/eng.traineddata"
)

add_executable(ocr_golden
    ocr_golden.cpp
    "${CMAKE_SOURCE_DIR}/RuneHelper/core/Logger.cpp"
    "${CMAKE_SOURCE_DIR}/RuneHelper/ocr/LootParser.cpp"
    "${CMAKE_SOURCE_DIR}/RuneHelper/ocr/NameNormalizer.cpp"
    "${CMAKE_SOURCE_DIR}/RuneHelper/ocr/OCR.cpp"
    "${CMAKE_SOURCE_DIR}/RuneHelper/platform/linux/PlatformPaths.cpp"
)

add_dependencies(ocr_golden ocr_golden_tessdata)

target_include_directories(ocr_golden PRIVATE "${CMAKE_SOURCE_DIR}/RuneHelper")

target_compile_definitions(ocr_golden PRIVATE
    RUNEHELPER_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(ocr_golden PRIVATE
    ${OpenCV_LIBS}
    Tesseract::libtesseract
    nlohmann_json::nlohmann_json
)

add_test(
    NAME ocr_golden
    COMMAND ocr_golden
        "${RUNEHELPER_TESSDATA_DIR}"
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

add_custom_target(bless_ocr_golden
    COMMAND ocr_golden
        "${RUNEHELPER_TESSDATA_DIR}"
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden"
        --bless
    DEPENDS ocr_golden
    VERBATIM
)
