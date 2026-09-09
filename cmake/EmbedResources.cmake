set(RUNEHELPER_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/RuneHelper/resources")

set(RUNEHELPER_EMBEDDED_FILES "${RUNEHELPER_RESOURCE_DIR}/eng.traineddata_fast")

file(GLOB RUNEHELPER_RUNE_TEMPLATES "${RUNEHELPER_RESOURCE_DIR}/runes/*.png")
list(SORT RUNEHELPER_RUNE_TEMPLATES)
list(APPEND RUNEHELPER_EMBEDDED_FILES ${RUNEHELPER_RUNE_TEMPLATES})

set(RUNEHELPER_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${RUNEHELPER_EMBED_DIR}")

set(RUNEHELPER_EMBED_EXTERNS "")
set(RUNEHELPER_EMBED_ENTRIES "")

foreach(resource ${RUNEHELPER_EMBEDDED_FILES})
    get_filename_component(resource_name "${resource}" NAME)
    get_filename_component(resource_dir "${resource}" DIRECTORY)
    string(MAKE_C_IDENTIFIER "${resource_name}" resource_symbol)

    set(resource_object "${RUNEHELPER_EMBED_DIR}/${resource_name}.o")

    add_custom_command(
        OUTPUT "${resource_object}"
        COMMAND "${CMAKE_LINKER}" -r -b binary -o "${resource_object}" "${resource_name}"
        WORKING_DIRECTORY "${resource_dir}"
        DEPENDS "${resource}"
        VERBATIM
    )

    set_source_files_properties("${resource_object}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    list(APPEND RUNEHELPER_GENERATED_SOURCES "${resource_object}")

    string(APPEND RUNEHELPER_EMBED_EXTERNS
        "extern const unsigned char _binary_${resource_symbol}_start[];\n"
        "extern const unsigned char _binary_${resource_symbol}_end[];\n"
    )

    string(APPEND RUNEHELPER_EMBED_ENTRIES
        "        {\"${resource_name}\", _binary_${resource_symbol}_start, _binary_${resource_symbol}_end},\n"
    )
endforeach()

file(WRITE "${RUNEHELPER_EMBED_DIR}/EmbeddedResources.cpp"
"#include \"platform/linux/EmbeddedResources.h\"

extern \"C\"
{
${RUNEHELPER_EMBED_EXTERNS}}

const std::vector<EmbeddedResource>& GetEmbeddedResources()
{
    static const std::vector<EmbeddedResource> resources = {
${RUNEHELPER_EMBED_ENTRIES}    };

    return resources;
}
")

list(APPEND RUNEHELPER_GENERATED_SOURCES "${RUNEHELPER_EMBED_DIR}/EmbeddedResources.cpp")
