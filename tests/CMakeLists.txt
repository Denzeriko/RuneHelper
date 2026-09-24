list(TRANSFORM RUNEHELPER_CORE_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE RUNEHELPER_TEST_CORE)
list(TRANSFORM RUNEHELPER_CORE_PLATFORM_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE RUNEHELPER_TEST_PLATFORM)

set(RUNEHELPER_TEST_SOURCES
    ${RUNEHELPER_TEST_CORE}
    ${RUNEHELPER_TEST_PLATFORM}
    ${RUNEHELPER_EMBEDDED_SOURCES}
)

set(RUNEHELPER_TEST_LIBRARIES
    ${OpenCV_LIBS}
    cpr::cpr
    nlohmann_json::nlohmann_json
)

foreach(object IN LISTS RUNEHELPER_EMBEDDED_OBJECTS)
    set_source_files_properties("${object}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endforeach()

function(runehelper_add_program name source)
    add_executable(${name} ${source} ${RUNEHELPER_TEST_SOURCES})

    add_dependencies(${name} runehelper_embedded_resources)

    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/RuneHelper" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(${name} PRIVATE ${RUNEHELPER_WARNINGS})

    target_compile_definitions(${name} PRIVATE
        RUNEHELPER_VERSION="${PROJECT_VERSION}"
        RUNEHELPER_VERSION_LABEL="${RUNEHELPER_VERSION_LABEL}"
        RUNEHELPER_COMMIT="${RUNEHELPER_COMMIT}"
    )

    target_link_libraries(${name} PRIVATE ${RUNEHELPER_TEST_LIBRARIES})
endfunction()

function(runehelper_add_test name)
    runehelper_add_program(${name} ${name}.cpp)
endfunction()

runehelper_add_test(ocr_golden)
runehelper_add_test(ocr_robustness)
runehelper_add_test(ocr_row_cache)
runehelper_add_test(ocr_rune_tiles)
runehelper_add_test(unit_tests)

runehelper_add_program(text_model_crops "${CMAKE_SOURCE_DIR}/tools/text-model/crops.cpp")

add_test(
    NAME unit_tests
    COMMAND unit_tests
)

add_test(
    NAME ocr_row_cache
    COMMAND ocr_row_cache
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
)

add_test(
    NAME ocr_golden
    COMMAND ocr_golden
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden"
        "${CMAKE_CURRENT_SOURCE_DIR}/truth"
)

foreach(language IN LISTS RUNEHELPER_TEXT_MODEL_LANGUAGES)
    add_test(
        NAME ocr_golden_${language}
        COMMAND ocr_golden
            "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/${language}/panels"
            "${CMAKE_CURRENT_SOURCE_DIR}/${language}/golden"
            "${CMAKE_CURRENT_SOURCE_DIR}/${language}/truth"
            --language ${language}
    )
endforeach()

add_test(
    NAME ocr_rune_tiles
    COMMAND ocr_rune_tiles
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
)

add_test(
    NAME ocr_robustness
    COMMAND ocr_robustness
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/truth"
)

add_custom_target(runehelper_tests
    DEPENDS ocr_golden ocr_robustness ocr_row_cache ocr_rune_tiles unit_tests
)

set(RUNEHELPER_BLESS_COMMANDS
    COMMAND ocr_golden
        "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden"
        "${CMAKE_CURRENT_SOURCE_DIR}/truth"
        --bless
)

foreach(language IN LISTS RUNEHELPER_TEXT_MODEL_LANGUAGES)
    list(APPEND RUNEHELPER_BLESS_COMMANDS
        COMMAND ocr_golden
            "${CMAKE_SOURCE_DIR}/RuneHelper/resources/combinations.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/${language}/panels"
            "${CMAKE_CURRENT_SOURCE_DIR}/${language}/golden"
            "${CMAKE_CURRENT_SOURCE_DIR}/${language}/truth"
            --bless
            --language ${language}
    )
endforeach()

add_custom_target(bless_ocr_golden
    ${RUNEHELPER_BLESS_COMMANDS}
    DEPENDS ocr_golden
    VERBATIM
)
