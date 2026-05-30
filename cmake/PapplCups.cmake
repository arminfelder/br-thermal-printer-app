#    Copyright (C) 2026  Armin Felder
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.

include_guard()

# ---------------------------------------------------------------------------
# Helper: resolve a pkg-config module to its static .a and create an
# INTERFACE IMPORTED target named <alias>.
#   _name        — pkg-config module name, e.g. "pappl"
#   _alias       — CMake target name, e.g. "PAPPL::pappl"
#   _fatal_hint  — extra text appended to the FATAL_ERROR when .a is missing
# ---------------------------------------------------------------------------
function(_add_static_pkg_target _name _alias _fatal_hint)
    string(TOUPPER "${_name}" _upper)

    pkg_check_modules(${_upper} REQUIRED STATIC ${_name})

    find_library(${_upper}_STATIC_LIB NAMES "lib${_name}.a"
            PATHS "${${_upper}_LIBRARY_DIRS}" NO_DEFAULT_PATH)
    if(NOT ${_upper}_STATIC_LIB)
        message(FATAL_ERROR
                "lib${_name}.a not found in ${${_upper}_LIBRARY_DIRS} — ${_fatal_hint}")
    endif()

    # Replace the bare module name with the resolved .a path so the linker
    # gets an absolute path rather than -l<name> (which would pick the .so).
    set(_deps "${${_upper}_STATIC_LIBRARIES}")
    list(REMOVE_ITEM _deps "${_name}")

    add_library(${_alias} INTERFACE IMPORTED)
    set_target_properties(${_alias} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${${_upper}_STATIC_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES      "${${_upper}_STATIC_LIB};${_deps}"
    )
endfunction()

# ---------------------------------------------------------------------------
# System packages — static linking
# ---------------------------------------------------------------------------
function(_pappl_cups_system_static)
    find_package(PkgConfig REQUIRED)
    message(STATUS "[PapplCups] Linking PAPPL and CUPS statically")
    _add_static_pkg_target(pappl PAPPL::pappl "install libpappl-dev")
    _add_static_pkg_target(cups  CUPS::cups   "install libcups2-dev")
endfunction()

# ---------------------------------------------------------------------------
# System packages — dynamic linking
# ---------------------------------------------------------------------------
function(_pappl_cups_system_dynamic)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(PAPPL REQUIRED IMPORTED_TARGET pappl)
    add_library(PAPPL::pappl INTERFACE IMPORTED)
    set_target_properties(PAPPL::pappl PROPERTIES
            INTERFACE_LINK_LIBRARIES PkgConfig::PAPPL
    )

    pkg_check_modules(CUPS IMPORTED_TARGET cups)
    if(CUPS_FOUND)
        add_library(CUPS::cups INTERFACE IMPORTED)
        set_target_properties(CUPS::cups PROPERTIES
                INTERFACE_LINK_LIBRARIES PkgConfig::CUPS
        )
    else()
        find_package(CUPS REQUIRED MODULE)
        add_library(CUPS::cups INTERFACE IMPORTED)
        set_target_properties(CUPS::cups PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${CUPS_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES      "${CUPS_LIBRARIES}"
        )
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Build CUPS + PAPPL from source via ExternalProject
# ---------------------------------------------------------------------------
function(_pappl_cups_from_source)
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NOT NPROC OR NPROC EQUAL 0)
        set(NPROC 1)
    endif()

    include(FetchContent)
    include(ExternalProject)

    # -------------------------------------------------------------------------
    # CUPS
    # -------------------------------------------------------------------------
    set(_cups_install   "${CMAKE_BINARY_DIR}/cups-prefix/install")
    set(_cups_pkgconfig "${_cups_install}/lib/pkgconfig")
    set(_cups_lib       "${_cups_install}/lib64/libcups.so.2")
    set(_cups_image_lib "${_cups_install}/lib64/libcupsimage.so.2")

    file(MAKE_DIRECTORY "${_cups_install}/include"
                        "${_cups_install}/lib64"
                        "${_cups_install}/etc/rc.d")

    set(_cups_flags
            --prefix=${_cups_install}
            --with-rcdir=${_cups_install}/etc/rc.d
            --libdir=${_cups_install}/lib64
    )
    set(_cups_cflags "")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _cups_flags --enable-debug)
        set(_cups_cflags "-g -O0")
    endif()

    FetchContent_Declare(cups_src
            GIT_REPOSITORY https://github.com/OpenPrinting/cups.git
            GIT_TAG        v2.4.16
    )
    FetchContent_MakeAvailable(cups_src)

    ExternalProject_Add(cups_proj
            SOURCE_DIR        "${cups_src_SOURCE_DIR}"
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
                              CFLAGS=${_cups_cflags}
                              ./configure ${_cups_flags}
            BUILD_COMMAND     make -j${NPROC}
            INSTALL_COMMAND   make install-libs
            BUILD_IN_SOURCE   1
            BUILD_BYPRODUCTS  "${_cups_lib}" "${_cups_image_lib}"
    )

    add_library(cups_external SHARED IMPORTED)
    set_target_properties(cups_external PROPERTIES
            IMPORTED_LOCATION             "${_cups_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_cups_install}/include"
    )
    add_dependencies(cups_external cups_proj)

    add_library(CUPS::cups INTERFACE IMPORTED)
    set_target_properties(CUPS::cups PROPERTIES
            INTERFACE_LINK_LIBRARIES cups_external
    )

    # -------------------------------------------------------------------------
    # PAPPL (depends on CUPS)
    # -------------------------------------------------------------------------
    set(_pappl_install "${CMAKE_BINARY_DIR}/pappl-prefix/install")
    set(_pappl_lib     "${_pappl_install}/lib/libpappl.so")

    file(MAKE_DIRECTORY "${_pappl_install}/include" "${_pappl_install}/lib")

    set(_pappl_flags --prefix=${_pappl_install})
    set(_pappl_cflags "")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _pappl_flags --enable-debug)
        set(_pappl_cflags "-g -O0")
    endif()

    FetchContent_Declare(pappl_src
            GIT_REPOSITORY https://github.com/michaelrsweet/pappl.git
            GIT_TAG        v1.4.10
    )
    FetchContent_MakeAvailable(pappl_src)

    ExternalProject_Add(pappl_proj
            SOURCE_DIR        "${pappl_src_SOURCE_DIR}"
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
                              PKG_CONFIG_PATH=${_cups_pkgconfig}:$ENV{PKG_CONFIG_PATH}
                              CPPFLAGS=-I${_cups_install}/include
                              LDFLAGS=-L${_cups_install}/lib
                              CFLAGS=${_pappl_cflags}
                              ./configure ${_pappl_flags}
            BUILD_COMMAND     make -j${NPROC}
            INSTALL_COMMAND   make install
            BUILD_IN_SOURCE   1
            BUILD_BYPRODUCTS  "${_pappl_lib}"
    )
    add_dependencies(pappl_proj cups_proj)

    add_library(pappl_external SHARED IMPORTED)
    set_target_properties(pappl_external PROPERTIES
            IMPORTED_LOCATION             "${_pappl_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_pappl_install}/include"
    )
    add_dependencies(pappl_external pappl_proj)

    add_library(PAPPL::pappl INTERFACE IMPORTED)
    set_target_properties(PAPPL::pappl PROPERTIES
            INTERFACE_LINK_LIBRARIES pappl_external
    )
endfunction()

# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------
function(pappl_cups_setup)
    if(BUILD_PAPPL_FROM_SOURCE)
        _pappl_cups_from_source()
    elseif(STATIC_PAPPL_CUPS)
        _pappl_cups_system_static()
    else()
        _pappl_cups_system_dynamic()
    endif()
endfunction()
