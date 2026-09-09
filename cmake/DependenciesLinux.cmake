set(OpenGL_GL_PREFERENCE GLVND)

include(FetchContent)
include(EmbedResources)

find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
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
    pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

    if(NOT WAYLAND_SCANNER)
        find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)
    endif()

    FetchContent_Declare(
        wlr_protocols
        GIT_REPOSITORY https://gitlab.freedesktop.org/wlroots/wlr-protocols.git
        GIT_TAG bf4fc79abc359eea5a0edec0ac6d4a2b2955f82a
    )

    FetchContent_MakeAvailable(wlr_protocols)

    pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

    set(RUNEHELPER_WAYLAND_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
    file(MAKE_DIRECTORY "${RUNEHELPER_WAYLAND_PROTOCOL_DIR}")

    set(RUNEHELPER_WAYLAND_PROTOCOL_XMLS
        "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml"
        "${wlr_protocols_SOURCE_DIR}/unstable/wlr-screencopy-unstable-v1.xml"
        "${wlr_protocols_SOURCE_DIR}/unstable/wlr-layer-shell-unstable-v1.xml"
    )

    foreach(protocol_xml ${RUNEHELPER_WAYLAND_PROTOCOL_XMLS})
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
    set(RUNEHELPER_PLATFORM_LIBRARIES PkgConfig::WaylandClient)

    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
else()
    find_package(X11 REQUIRED)

    set(RUNEHELPER_PLATFORM_LIBRARIES ${X11_LIBRARIES})

    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
endif()

find_package(glfw3 QUIET)

if(NOT glfw3_FOUND)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )

    FetchContent_MakeAvailable(glfw)
endif()

FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.14.2
)

set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cpr)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.9
)

FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
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
    ${LAPACK_LIBRARIES}
    ${BLAS_LIBRARIES}
)

set(RUNEHELPER_DEFINITIONS
    RUNEHELPER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
