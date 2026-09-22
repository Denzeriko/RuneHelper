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

list(TRANSFORM RUNEHELPER_CORE_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE RUNEHELPER_TEST_CORE)
list(TRANSFORM RUNEHELPER_CORE_PLATFORM_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE RUNEHELPER_TEST_PLATFORM)

set(RUNEHELPER_TEST_SOURCES
    ${RUNEHELPER_TEST_CORE}
    ${RUNEHELPER_TEST_PLATFORM}
    ${RUNEHELPER_EMBEDDED_SOURCES}
)

set(RUNEHELPER_TEST_LIBRARIES
    ${OpenCV_LIBS}
    Tesseract::libtesseract
    cpr::cpr
    nlohmann_json::nlohmann_json
)

foreach(object IN LISTS RUNEHELPER_EMBEDDED_OBJECTS)
    set_source_files_properties("${object}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endforeach()

function(runehelper_add_test name)
    add_executable(${name} ${name}.cpp ${RUNEHELPER_TEST_SOURCES})

    add_dependencies(${name} ocr_golden_tessdata runehelper_embedded_resources)

    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/RuneHelper")
    target_compile_options(${name} PRIVATE ${RUNEHELPER_WARNINGS})

    target_compile_definitions(${name} PRIVATE
        RUNEHELPER_VERSION="${PROJECT_VERSION}"
        RUNEHELPER_VERSION_LABEL="${RUNEHELPER_VERSION_LABEL}"
        RUNEHELPER_COMMIT="${RUNEHELPER_COMMIT}"
    )

    target_link_libraries(${name} PRIVATE ${RUNEHELPER_TEST_LIBRARIES})
endfunction()

runehelper_add_test(ocr_golden)
runehelper_add_test(ocr_row_cache)
runehelper_add_test(unit_tests)

add_test(
    NAME unit_tests
    COMMAND unit_tests
)

add_test(
    NAME ocr_row_cache
    COMMAND ocr_row_cache
        "${RUNEHELPER_TESSDATA_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
)

add_test(
    NAME ocr_golden
    COMMAND ocr_golden
        "${RUNEHELPER_TESSDATA_DIR}"
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden"
        "${CMAKE_CURRENT_SOURCE_DIR}/truth"
)

add_custom_target(runehelper_tests
    DEPENDS ocr_golden ocr_row_cache unit_tests
)

add_custom_target(bless_ocr_golden
    COMMAND ocr_golden
        "${RUNEHELPER_TESSDATA_DIR}"
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden"
        "${CMAKE_CURRENT_SOURCE_DIR}/truth"
        --bless
    DEPENDS ocr_golden
    VERBATIM
)
