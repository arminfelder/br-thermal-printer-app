cmake_minimum_required(VERSION 3.31)

include(../cmake/IncludeBlobs.cmake)

file(GLOB MEDIA_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/drivers/td2000/data/media/*"
)

set(GENERATED_INCS)

foreach(media_dir IN LISTS MEDIA_DIRS)
    if(NOT IS_DIRECTORY "${media_dir}")
        continue()
    endif()

    file(GLOB MEDIA_BIN_FILES CONFIGURE_DEPENDS
            "${media_dir}/*.bin"
    )

    get_filename_component(media_dir_name "${media_dir}" NAME)
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${media_dir_name}")

    foreach(bin_file IN LISTS MEDIA_BIN_FILES)
        get_filename_component(stem "${bin_file}" NAME_WE)
        string(MAKE_C_IDENTIFIER "${stem}" var_name)

        set(out_file "${output_dir}/${stem}.inc")
        generate_bin_inc("${bin_file}" "${out_file}" "${var_name}" "drivers::td2000::media::blobs::${media_dir_name}")

        list(APPEND GENERATED_INCS "${out_file}")
    endforeach()
endforeach()

set(GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/media_generated.inc")

file(WRITE "${GENERATED_HEADER}" "// Auto-generated file. Do not edit.\n")
get_filename_component(generated_header_dir "${GENERATED_HEADER}" DIRECTORY)
foreach(inc_file IN LISTS GENERATED_INCS)
    file(RELATIVE_PATH inc_rel_path "${generated_header_dir}" "${inc_file}")
    file(APPEND "${GENERATED_HEADER}" "#include \"${inc_rel_path}\"\n")
endforeach()