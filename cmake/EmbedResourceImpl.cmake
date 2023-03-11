function(embed_resource_impl)
    cmake_parse_arguments(EMBED "" "NAMESPACE;OUTPUT" "INPUT_FILENAME;INPUT_VARNAME" ${ARGN})

    string(APPEND IMPL_CONTENT
            "#include \"${EMBED_OUTPUT}.h\"\n"
            "namespace ${EMBED_NAMESPACE} {\n"
    )

    foreach(INPUT_FILENAME INPUT_VARNAME IN ZIP_LISTS EMBED_INPUT_FILENAME EMBED_INPUT_VARNAME)
        file(READ "${INPUT_FILENAME}" HEX_CONTENT HEX)
        string(REPEAT "[0-9a-f]" 32 PATTERN)
        string(REGEX REPLACE "(${PATTERN})" "\\1\n" CONTENT "${HEX_CONTENT}")

        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " CONTENT "${CONTENT}")

        string(REGEX REPLACE ", $" "" CONTENT "${CONTENT}")

        string(APPEND IMPL_CONTENT
                "// NOLINTNEXTLINE\n"
                "static const unsigned char ${INPUT_VARNAME}Data[] = { ${CONTENT} };\n"
                "const std::string_view ${INPUT_VARNAME}(reinterpret_cast<const char*>(${INPUT_VARNAME}Data), sizeof(${INPUT_VARNAME}Data));\n"
        )
    endforeach()
    string(APPEND IMPL_CONTENT
        "}  // namespace ${EMBED_NAMESPACE}\n"
    )
    file(WRITE "${EMBED_OUTPUT}.cc" "${IMPL_CONTENT}")
endfunction()

embed_resource_impl(
        NAMESPACE ${NAMESPACE}
        OUTPUT ${OUTPUT}
        INPUT_FILENAME ${ASSET_FILENAME}
        INPUT_VARNAME ${ASSET_VARNAME}
)
