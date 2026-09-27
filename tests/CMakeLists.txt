list(TRANSFORM RUNEHELPER_CORE_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE RUNEHELPER_TEST_CORE)
list(TRANSFORM RUNEHELPER_CORE_PLATFORM_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE RUNEHELPER_TEST_PLATFORM)

foreach(object IN LISTS RUNEHELPER_EMBEDDED_OBJECTS)
    set_source_files_properties("${object}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endforeach()

add_library(runehelper_test_support STATIC
    ${RUNEHELPER_TEST_CORE}
    ${RUNEHELPER_TEST_PLATFORM}
    ${RUNEHELPER_EMBEDDED_SOURCES}
    "${CMAKE_SOURCE_DIR}/RuneHelper/core/Feature.cpp"
)

add_dependencies(runehelper_test_support runehelper_embedded_resources)

target_include_directories(runehelper_test_support PUBLIC "${CMAKE_SOURCE_DIR}/RuneHelper" "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_compile_options(runehelper_test_support PUBLIC ${RUNEHELPER_WARNINGS})

target_compile_definitions(runehelper_test_support PUBLIC
    RUNEHELPER_VERSION="${PROJECT_VERSION}"
    RUNEHELPER_VERSION_LABEL="${RUNEHELPER_VERSION_LABEL}"
    RUNEHELPER_COMMIT="${RUNEHELPER_COMMIT}"
)

target_link_libraries(runehelper_test_support PUBLIC
    ${OpenCV_LIBS}
    cpr::cpr
    nlohmann_json::nlohmann_json
)

function(runehelper_add_program name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE runehelper_test_support)
endfunction()

runehelper_add_program(ocr_golden ocr/ocr_golden.cpp)
runehelper_add_program(ocr_robustness ocr/ocr_robustness.cpp)
runehelper_add_program(ocr_row_cache ocr/ocr_row_cache.cpp)
runehelper_add_program(ocr_rune_tiles ocr/ocr_rune_tiles.cpp)
runehelper_add_program(unit_tests unit_tests.cpp)
runehelper_add_program(ocr_service_cancel services/ocr_service_cancel.cpp)
runehelper_add_program(update_checker services/update_checker.cpp)
runehelper_add_program(price_cache services/price_cache.cpp)

add_test(NAME price_cache COMMAND price_cache)
set_tests_properties(price_cache PROPERTIES TIMEOUT 60)

target_sources(update_checker PRIVATE
    "${CMAKE_SOURCE_DIR}/RuneHelper/core/UpdateChecker.cpp"
    "${CMAKE_SOURCE_DIR}/RuneHelper/core/UpdateInstaller.cpp"
)
target_compile_definitions(update_checker PRIVATE RUNEHELPER_BUILD_VARIANT="linux-${RUNEHELPER_LINUX_BACKEND}")

add_test(NAME update_checker COMMAND update_checker)
set_tests_properties(update_checker PROPERTIES TIMEOUT 60)

target_sources(ocr_service_cancel PRIVATE
    "${CMAKE_SOURCE_DIR}/RuneHelper/core/OcrService.cpp"
    "${CMAKE_SOURCE_DIR}/RuneHelper/core/ScreenCaptureService.cpp"
)

runehelper_add_program(text_model_crops "${CMAKE_SOURCE_DIR}/tools/text-model/crops.cpp")

add_test(
    NAME unit_tests
    COMMAND unit_tests
)

add_test(
    NAME ocr_service_cancel
    COMMAND ocr_service_cancel "${CMAKE_CURRENT_SOURCE_DIR}/data/en/panels/2560/test00.png"
)
set_tests_properties(ocr_service_cancel PROPERTIES TIMEOUT 120)

add_test(
    NAME ocr_row_cache
    COMMAND ocr_row_cache
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/panels"
)

add_test(
    NAME ocr_golden
    COMMAND ocr_golden
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/golden"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/truth"
)

foreach(language IN LISTS RUNEHELPER_TEXT_MODEL_LANGUAGES)
    add_test(
        NAME ocr_golden_${language}
        COMMAND ocr_golden
            "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${language}/panels"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${language}/golden"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${language}/truth"
            --language ${language}
    )
endforeach()

add_test(
    NAME ocr_rune_tiles
    COMMAND ocr_rune_tiles
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/panels"
)

add_test(
    NAME ocr_robustness
    COMMAND ocr_robustness
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/truth"
)

add_custom_target(runehelper_tests
    DEPENDS ocr_golden ocr_robustness ocr_row_cache ocr_rune_tiles unit_tests ocr_service_cancel update_checker price_cache
)

set(RUNEHELPER_BLESS_COMMANDS
    COMMAND ocr_golden
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/golden"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/en/truth"
        --bless
)

foreach(language IN LISTS RUNEHELPER_TEXT_MODEL_LANGUAGES)
    list(APPEND RUNEHELPER_BLESS_COMMANDS
        COMMAND ocr_golden
            "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${language}/panels"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${language}/golden"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${language}/truth"
            --bless
            --language ${language}
    )
endforeach()

add_custom_target(bless_ocr_golden
    ${RUNEHELPER_BLESS_COMMANDS}
    DEPENDS ocr_golden
    VERBATIM
)
