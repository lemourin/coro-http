set(EMBED_RESOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

function(embed_resource_header)
    cmake_parse_arguments(EMBED "" "NAMESPACE;OUTPUT" "INPUT" ${ARGN})

    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" HEADER_GUARD ${EMBED_OUTPUT})

    string(APPEND HEADER_CONTENT
           "#ifndef ${HEADER_GUARD}\n"
           "#define ${HEADER_GUARD}\n"
           "#include <string_view>\n"
           "namespace ${EMBED_NAMESPACE} {\n")
    foreach(ENTRY ${EMBED_INPUT})
        string(APPEND HEADER_CONTENT
               "extern const std::string_view ${ENTRY};\n"
        )
    endforeach()
    string(APPEND HEADER_CONTENT
           "} // namespace ${EMBED_NAMESPACE}\n"
           "#endif // ${HEADER_GUARD}\n")
    file(GENERATE OUTPUT "${EMBED_OUTPUT}" CONTENT "${HEADER_CONTENT}")
endfunction()

function(add_resource_library TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 EMBED "" "NAMESPACE;OUTPUT" "INPUT")
    list(LENGTH EMBED_INPUT EMBED_INPUT_LENGTH)
    foreach(I RANGE 1 ${EMBED_INPUT_LENGTH} 2)
        math(EXPR PREVIOUS "${I}-1")
        list(GET EMBED_INPUT ${PREVIOUS} INPUT_VARNAME)
        list(GET EMBED_INPUT ${I} INPUT_FILENAME)
        list(APPEND ASSET_VARNAME ${INPUT_VARNAME})
        list(APPEND ASSET_FILENAME ${INPUT_FILENAME})
    endforeach()
    get_filename_component(OUTPUT_DIRECTORY ${EMBED_OUTPUT} DIRECTORY)
    get_filename_component(OUTPUT_BASENAME ${EMBED_OUTPUT} NAME_WE)
    set(OUTPUT_BASENAME ${OUTPUT_DIRECTORY}/${OUTPUT_BASENAME})
    add_custom_command(
        OUTPUT ${OUTPUT_BASENAME}.cc
        COMMAND
            ${CMAKE_COMMAND}
                "-DNAMESPACE=${EMBED_NAMESPACE}"
                "-DOUTPUT=${OUTPUT_BASENAME}"
                "-DASSET_FILENAME=${ASSET_FILENAME}"
                "-DASSET_VARNAME=${ASSET_VARNAME}"
                "-DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}"
                -P "${EMBED_RESOURCE_DIR}/EmbedResourceImpl.cmake"
        DEPENDS
            ${ASSET_FILENAME}
        WORKING_DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
    )
    embed_resource_header(
        OUTPUT ${EMBED_OUTPUT}
        NAMESPACE ${EMBED_NAMESPACE}
        INPUT ${ASSET_VARNAME}
    )
    add_library(
        ${TARGET} OBJECT
        ${EMBED_OUTPUT}
        ${OUTPUT_BASENAME}.cc
    )
    target_compile_features(${TARGET} PUBLIC cxx_std_17)
endfunction()
