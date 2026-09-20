if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(WIN32)
        set(RUNEHELPER_WARNINGS /W4 /EHsc)
        set(RUNEHELPER_WARNING_ERRORS /WX)
    else()
        set(RUNEHELPER_WARNINGS -Wall -Wextra -Wpedantic)
        set(RUNEHELPER_WARNING_ERRORS -Werror)
    endif()
elseif(MSVC)
    set(RUNEHELPER_WARNINGS /MP /W4 /EHsc)
    set(RUNEHELPER_WARNING_ERRORS /WX)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(RUNEHELPER_WARNINGS -Wall -Wextra -Wpedantic)
    set(RUNEHELPER_WARNING_ERRORS -Werror -Wno-error=restrict)
endif()

if(RUNEHELPER_WERROR)
    list(APPEND RUNEHELPER_WARNINGS ${RUNEHELPER_WARNING_ERRORS})
endif()

target_compile_options(RuneHelper PRIVATE ${RUNEHELPER_WARNINGS})

if(UNIX AND NOT APPLE)
    target_compile_options(RuneHelper PRIVATE
        -ffunction-sections
        -fdata-sections
    )

    target_link_options(RuneHelper PRIVATE
        -Wl,--gc-sections
    )
endif()

if(RUNEHELPER_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT RUNEHELPER_IPO_SUPPORTED OUTPUT RUNEHELPER_IPO_MESSAGE LANGUAGES CXX)

    if(RUNEHELPER_IPO_SUPPORTED)
        set_property(TARGET RuneHelper PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "Link time optimisation is unavailable: ${RUNEHELPER_IPO_MESSAGE}")
    endif()
endif()
