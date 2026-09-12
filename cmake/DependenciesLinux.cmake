set(OpenGL_GL_PREFERENCE GLVND)

include(FetchContent)
include(EmbedResources)

set(RUNEHELPER_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(RUNEHELPER_SUBMODULE_HINT "run: git submodule update --init --recursive")

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Tesseract QUIET)

if(NOT Tesseract_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Tesseract REQUIRED IMPORTED_TARGET tesseract lept)
    add_library(Tesseract::libtesseract ALIAS PkgConfig::Tesseract)
endif()

find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)

if(RUNEHELPER_LINUX_BACKEND STREQUAL "wayland")
    enable_language(C)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
    pkg_check_modules(DBus REQUIRED IMPORTED_TARGET dbus-1)
    pkg_check_modules(PipeWire REQUIRED IMPORTED_TARGET libpipewire-0.3)
    pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

    if(NOT WAYLAND_SCANNER)
        find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)
    endif()

    set(RUNEHELPER_WLR_PROTOCOLS_DIR "" CACHE PATH "Directory holding the wlr-protocols XML tree")

    if(NOT RUNEHELPER_WLR_PROTOCOLS_DIR)
        pkg_check_modules(WlrProtocols QUIET wlr-protocols)

        if(WlrProtocols_FOUND)
            pkg_get_variable(RUNEHELPER_WLR_PROTOCOLS_DIR wlr-protocols pkgdatadir)
        endif()
    endif()

    if(NOT RUNEHELPER_WLR_PROTOCOLS_DIR)
        FetchContent_Declare(
            wlr_protocols
            GIT_REPOSITORY https://gitlab.freedesktop.org/wlroots/wlr-protocols.git
            GIT_TAG bf4fc79abc359eea5a0edec0ac6d4a2b2955f82a
        )

        FetchContent_MakeAvailable(wlr_protocols)

        set(RUNEHELPER_WLR_PROTOCOLS_DIR "${wlr_protocols_SOURCE_DIR}")
    endif()

    pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

    set(RUNEHELPER_WAYLAND_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
    file(MAKE_DIRECTORY "${RUNEHELPER_WAYLAND_PROTOCOL_DIR}")

    set(RUNEHELPER_WAYLAND_PROTOCOL_XMLS
        "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml"
        "${RUNEHELPER_WLR_PROTOCOLS_DIR}/unstable/wlr-screencopy-unstable-v1.xml"
        "${RUNEHELPER_WLR_PROTOCOLS_DIR}/unstable/wlr-layer-shell-unstable-v1.xml"
    )

    foreach(protocol_xml ${RUNEHELPER_WAYLAND_PROTOCOL_XMLS})
        if(NOT EXISTS "${protocol_xml}")
            message(FATAL_ERROR "Wayland protocol XML not found: ${protocol_xml}")
        endif()

        get_filename_component(protocol "${protocol_xml}" NAME_WE)
        set(protocol_header "${RUNEHELPER_WAYLAND_PROTOCOL_DIR}/${protocol}-client-protocol.h")
        set(protocol_code "${RUNEHELPER_WAYLAND_PROTOCOL_DIR}/${protocol}-protocol.c")

        add_custom_command(
            OUTPUT "${protocol_header}"
            COMMAND "${WAYLAND_SCANNER}" client-header "${protocol_xml}" "${protocol_header}"
            DEPENDS "${protocol_xml}"
            VERBATIM
        )

        add_custom_command(
            OUTPUT "${protocol_code}"
            COMMAND "${WAYLAND_SCANNER}" private-code "${protocol_xml}" "${protocol_code}"
            DEPENDS "${protocol_xml}"
            VERBATIM
        )

        list(APPEND RUNEHELPER_GENERATED_SOURCES "${protocol_header}" "${protocol_code}")
    endforeach()

    set(RUNEHELPER_INCLUDE_DIRECTORIES "${RUNEHELPER_WAYLAND_PROTOCOL_DIR}")
    set(RUNEHELPER_PLATFORM_LIBRARIES
        PkgConfig::WaylandClient
        PkgConfig::DBus
        PkgConfig::PipeWire
    )

    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
else()
    find_package(X11 REQUIRED)

    set(RUNEHELPER_PLATFORM_LIBRARIES ${X11_LIBRARIES})

    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
endif()

find_package(glfw3 3.3 QUIET)

if(NOT glfw3_FOUND)
    if(NOT EXISTS "${RUNEHELPER_EXTERNAL_DIR}/glfw/CMakeLists.txt")
        message(FATAL_ERROR "Bundled GLFW is missing at ${RUNEHELPER_EXTERNAL_DIR}/glfw, ${RUNEHELPER_SUBMODULE_HINT}")
    endif()

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    add_subdirectory("${RUNEHELPER_EXTERNAL_DIR}/glfw" "${CMAKE_BINARY_DIR}/external/glfw" EXCLUDE_FROM_ALL)
endif()

find_package(cpr 1.10 CONFIG QUIET)

if(NOT cpr_FOUND)
    if(NOT EXISTS "${RUNEHELPER_EXTERNAL_DIR}/cpr/CMakeLists.txt")
        message(FATAL_ERROR "Bundled cpr is missing at ${RUNEHELPER_EXTERNAL_DIR}/cpr, ${RUNEHELPER_SUBMODULE_HINT}")
    endif()

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
    set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CPR_ENABLE_SSL ON CACHE BOOL "" FORCE)

    add_subdirectory("${RUNEHELPER_EXTERNAL_DIR}/cpr" "${CMAKE_BINARY_DIR}/external/cpr" EXCLUDE_FROM_ALL)
endif()

find_package(nlohmann_json 3.11 CONFIG QUIET)

if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
    FetchContent_MakeAvailable(json)
endif()

set(RUNEHELPER_IMGUI_DIR "${RUNEHELPER_EXTERNAL_DIR}/imgui" CACHE PATH "Dear ImGui source directory")

if(NOT EXISTS "${RUNEHELPER_IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui sources are missing at ${RUNEHELPER_IMGUI_DIR}, ${RUNEHELPER_SUBMODULE_HINT}")
endif()

file(STRINGS "${RUNEHELPER_IMGUI_DIR}/imgui.h" RUNEHELPER_IMGUI_VERSION_LINE REGEX "^#define[ \t]+IMGUI_VERSION_NUM[ \t]+[0-9]+")
string(REGEX MATCH "[0-9]+" RUNEHELPER_IMGUI_VERSION "${RUNEHELPER_IMGUI_VERSION_LINE}")

if(NOT RUNEHELPER_IMGUI_VERSION)
    message(FATAL_ERROR "Could not read IMGUI_VERSION_NUM from ${RUNEHELPER_IMGUI_DIR}/imgui.h")
endif()

if(RUNEHELPER_IMGUI_VERSION LESS 19090)
    message(FATAL_ERROR "Dear ImGui at ${RUNEHELPER_IMGUI_DIR} reports ${RUNEHELPER_IMGUI_VERSION}, RuneHelper needs 19090 (1.90.9) or newer")
endif()

if(RUNEHELPER_IMGUI_VERSION GREATER_EQUAL 19200)
    message(WARNING "Dear ImGui at ${RUNEHELPER_IMGUI_DIR} reports ${RUNEHELPER_IMGUI_VERSION}, RuneHelper is tested against 1.90.9 and 1.92 reworked fonts and texture identifiers")
endif()

add_library(imgui STATIC
    "${RUNEHELPER_IMGUI_DIR}/imgui.cpp"
    "${RUNEHELPER_IMGUI_DIR}/imgui_draw.cpp"
    "${RUNEHELPER_IMGUI_DIR}/imgui_tables.cpp"
    "${RUNEHELPER_IMGUI_DIR}/imgui_widgets.cpp"
    "${RUNEHELPER_IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${RUNEHELPER_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(imgui PUBLIC
    "${RUNEHELPER_IMGUI_DIR}"
    "${RUNEHELPER_IMGUI_DIR}/backends"
)

target_link_libraries(imgui PUBLIC
    glfw
    OpenGL::GL
)

set(RUNEHELPER_LIBRARIES
    ${OpenCV_LIBS}
    Tesseract::libtesseract
    cpr::cpr
    imgui
    glfw
    OpenGL::GL
    ${RUNEHELPER_PLATFORM_LIBRARIES}
    nlohmann_json::nlohmann_json
)
