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

function(pappl_cups_setup)
    # NPROC helper
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NOT NPROC OR NPROC EQUAL 0)
        set(NPROC 1)
    endif()

    if(NOT BUILD_PAPPL_FROM_SOURCE)
        find_package(PkgConfig REQUIRED)

        pkg_check_modules(PAPPL REQUIRED IMPORTED_TARGET pappl)
        add_library(PAPPL::pappl INTERFACE IMPORTED)
        set_target_properties(PAPPL::pappl PROPERTIES
                INTERFACE_LINK_LIBRARIES PkgConfig::PAPPL
        )

        pkg_check_modules(CUPS REQUIRED IMPORTED_TARGET cups)
        add_library(CUPS::cups INTERFACE IMPORTED)
        set_target_properties(CUPS::cups PROPERTIES
                INTERFACE_LINK_LIBRARIES PkgConfig::CUPS
        )
        return()
    endif()

    include(ExternalProject)

    ####################################################################
    ## CUPS
    ####################################################################
    set(CUPS_PREFIX       "${CMAKE_BINARY_DIR}/cups-prefix")
    set(CUPS_INSTALL_DIR  "${CUPS_PREFIX}/install")
    file(MAKE_DIRECTORY "${CUPS_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${CUPS_INSTALL_DIR}/lib64")
    file(MAKE_DIRECTORY "${CUPS_INSTALL_DIR}/etc/rc.d")

    set(CUPS_LIB "${CUPS_INSTALL_DIR}/lib64/libcups.so.2")
    set(CUPS_IMAGE_LIB "${CUPS_INSTALL_DIR}/lib64/libcupsimage.so.2")

    set(CUPS_LIBS
            "${CUPS_LIB}"
            "${CUPS_IMAGE_LIB}"
    )

    set(CUPS_PKGCONFIG_DIR "${CUPS_INSTALL_DIR}/lib/pkgconfig")

    set(CUPS_CONFIGURE_FLAGS
            --prefix=${CUPS_INSTALL_DIR}
            --with-rcdir=${CUPS_INSTALL_DIR}/etc/rc.d
            --libdir=${CUPS_INSTALL_DIR}/lib64
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND CUPS_CONFIGURE_FLAGS --enable-debug)
        set(CUPS_CFLAGS "-g -O0")
    endif()

        include(FetchContent)
        FetchContent_Declare(
                cups_src
                GIT_REPOSITORY https://github.com/OpenPrinting/cups.git
                GIT_TAG        v2.4.16
        )
        FetchContent_MakeAvailable(cups_src)
        set(CUPS_SOURCE_DIR "${cups_src_SOURCE_DIR}")

    ExternalProject_Add(cups_proj
            SOURCE_DIR        "${CUPS_SOURCE_DIR}"
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
            CFLAGS=${CUPS_CFLAGS}
            ./configure ${CUPS_CONFIGURE_FLAGS}
            BUILD_COMMAND     make -j${NPROC}
            INSTALL_COMMAND   make install-libs
            BUILD_IN_SOURCE   1
            BUILD_BYPRODUCTS  ${CUPS_LIBS}
    )

    add_library(cups_external SHARED IMPORTED)
    set_target_properties(cups_external PROPERTIES
            IMPORTED_LOCATION             ${CUPS_LIB}
            INTERFACE_INCLUDE_DIRECTORIES "${CUPS_INSTALL_DIR}/include"
    )

    add_library(CUPS::cups INTERFACE IMPORTED)
    set_target_properties(CUPS::cups PROPERTIES
            INTERFACE_LINK_LIBRARIES cups_external
    )

    ####################################################################
    ## PAPPL
    ####################################################################
    set(PAPPL_PREFIX       "${CMAKE_BINARY_DIR}/pappl-prefix")
    set(PAPPL_INSTALL_DIR  "${PAPPL_PREFIX}/install")

    file(MAKE_DIRECTORY "${PAPPL_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${PAPPL_INSTALL_DIR}/lib")

    set(PAPPL_LIB "${PAPPL_INSTALL_DIR}/lib/libpappl.so")

    set(PAPPL_CONFIGURE_FLAGS
            --prefix=${PAPPL_INSTALL_DIR}
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND PAPPL_CONFIGURE_FLAGS --enable-debug)
        set(PAPPL_CFLAGS "-g -O0")
    endif()


    include(FetchContent)
    FetchContent_Declare(
            pappl_src
            GIT_REPOSITORY https://github.com/michaelrsweet/pappl.git
            GIT_TAG        v1.4.10
    )
    FetchContent_MakeAvailable(pappl_src)
    set(PAPPL_SOURCE_DIR "${pappl_src_SOURCE_DIR}")

    set(PAPPL_ENV
            PKG_CONFIG_PATH=${CUPS_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}
            CPPFLAGS=-I${CUPS_INSTALL_DIR}/include
            LDFLAGS=-L${CUPS_INSTALL_DIR}/lib
    )

    ExternalProject_Add(pappl_proj
            SOURCE_DIR        "${PAPPL_SOURCE_DIR}"
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
            ${PAPPL_ENV}
            CFLAGS=${PAPPL_CFLAGS}
            ./configure ${PAPPL_CONFIGURE_FLAGS}
            BUILD_COMMAND     make -j${NPROC}
            INSTALL_COMMAND   make install
            BUILD_IN_SOURCE   1
            BUILD_BYPRODUCTS  "${PAPPL_LIB}"
    )

    add_dependencies(pappl_proj cups_proj)

    add_library(pappl_external SHARED IMPORTED)
    set_target_properties(pappl_external PROPERTIES
            IMPORTED_LOCATION             "${PAPPL_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${PAPPL_INSTALL_DIR}/include"
    )
    add_dependencies(pappl_external pappl_proj)

    add_library(PAPPL::pappl INTERFACE IMPORTED)
    set_target_properties(PAPPL::pappl PROPERTIES
            INTERFACE_LINK_LIBRARIES pappl_external
    )
endfunction()