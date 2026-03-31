function(generate_bin_inc input_bin output_inc var_name namespace_name)
    file(READ "${input_bin}" BIN_HEX HEX)

    string(LENGTH "${BIN_HEX}" BIN_HEX_LEN)
    math(EXPR BYTE_COUNT "${BIN_HEX_LEN} / 2")

    set(VAR_NAME ${var_name})
    set(NAMESPACE ${namespace_name})

    string(REPLACE "::" "_" GUARD_NS "${namespace_name}")
    string(TOUPPER "${GUARD_NS}${var_name}" HEADER_GUARD)

    set(BYTE_LIST "")
    set(COL 0)

    if(BYTE_COUNT GREATER 0)
        math(EXPR LAST_INDEX "${BYTE_COUNT} - 1")
        foreach(i RANGE 0 ${LAST_INDEX})
            math(EXPR HEX_POS "${i} * 2")
            string(SUBSTRING "${BIN_HEX}" ${HEX_POS} 2 BYTE_HEX)
            string(TOUPPER "${BYTE_HEX}" BYTE_HEX)

            if(COL EQUAL 0)
                string(APPEND BYTE_LIST "        ")
            endif()

            string(APPEND BYTE_LIST "0x${BYTE_HEX}")

            if(NOT i EQUAL LAST_INDEX)
                string(APPEND BYTE_LIST ", ")
            endif()

            math(EXPR COL "${COL} + 1")
            if(COL EQUAL 12 AND NOT i EQUAL LAST_INDEX)
                string(APPEND BYTE_LIST "\n")
                set(COL 0)
            endif()
        endforeach()
    endif()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/templates/media_blob.inc.in"
        "${output_inc}"
        @ONLY
    )

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input_bin}")
endfunction()