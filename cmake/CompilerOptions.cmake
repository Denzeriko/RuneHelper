if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(WIN32)
        target_compile_options(RuneHelper PRIVATE
            /W4
            /EHsc
        )
    else()
        target_compile_options(RuneHelper PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
elseif(MSVC)
    target_compile_options(RuneHelper PRIVATE
        /MP
        /W4
        /EHsc
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(RuneHelper PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(RuneHelper PRIVATE
        -ffunction-sections
        -fdata-sections
    )

    target_link_options(RuneHelper PRIVATE
        -Wl,--gc-sections
    )
endif()
