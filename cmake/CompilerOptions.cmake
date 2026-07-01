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