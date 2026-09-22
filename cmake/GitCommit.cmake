function(runehelper_git_dir out_var)
    set(${out_var} "" PARENT_SCOPE)

    set(entry "${CMAKE_CURRENT_SOURCE_DIR}/.git")

    if(NOT EXISTS "${entry}")
        return()
    endif()

    if(IS_DIRECTORY "${entry}")
        set(${out_var} "${entry}" PARENT_SCOPE)
        return()
    endif()

    file(READ "${entry}" link)
    string(STRIP "${link}" link)

    if(NOT link MATCHES "^gitdir:[ \t]*(.+)$")
        return()
    endif()

    set(resolved "${CMAKE_MATCH_1}")

    if(NOT IS_ABSOLUTE "${resolved}")
        get_filename_component(resolved "${CMAKE_CURRENT_SOURCE_DIR}/${resolved}" ABSOLUTE)
    endif()

    set(${out_var} "${resolved}" PARENT_SCOPE)
endfunction()

function(runehelper_read_commit out_var)
    set(${out_var} "" PARENT_SCOPE)

    runehelper_git_dir(git_dir)

    if(NOT git_dir OR NOT EXISTS "${git_dir}/HEAD")
        return()
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/HEAD")

    file(READ "${git_dir}/HEAD" head)
    string(STRIP "${head}" head)

    if(head MATCHES "^[0-9a-fA-F]+$")
        set(${out_var} "${head}" PARENT_SCOPE)
        return()
    endif()

    if(NOT head MATCHES "^ref:[ \t]*(.+)$")
        return()
    endif()

    set(ref "${CMAKE_MATCH_1}")

    if(EXISTS "${git_dir}/${ref}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/${ref}")

        file(READ "${git_dir}/${ref}" sha)
        string(STRIP "${sha}" sha)
        set(${out_var} "${sha}" PARENT_SCOPE)

        return()
    endif()

    if(NOT EXISTS "${git_dir}/packed-refs")
        return()
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/packed-refs")

    file(STRINGS "${git_dir}/packed-refs" packed REGEX "[ \t]${ref}$")

    foreach(line IN LISTS packed)
        if(line MATCHES "^([0-9a-fA-F]+)[ \t]")
            set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()
