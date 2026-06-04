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

    message(STATUS "[PapplCups] Building CUPS and PAPPL from source (static)")

    # When cross-compiling, forward the cross toolchain to the autotools builds
    # of CUPS/PAPPL: --host=<triplet> + CC/CXX. The triplet is derived from the
    # C compiler name (e.g. aarch64-linux-gnu-gcc -> aarch64-linux-gnu).
    set(_host_arg "")
    if(CMAKE_CROSSCOMPILING)
        get_filename_component(_cc_name "${CMAKE_C_COMPILER}" NAME)
        string(REGEX REPLACE "(-gcc|-cc|-clang)$" "" _host_triplet "${_cc_name}")
        if(_host_triplet)
            set(_host_arg --host=${_host_triplet})
        endif()
    endif()

    # Transitive dependencies of the static libcups.a / libpappl.a. These are
    # resolved against the target sysroot via pkg-config at configure time, so
    # they carry correct (cross) include/library paths. They link dynamically —
    # only CUPS and PAPPL themselves are static.
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    pkg_check_modules(_sys_ssl  REQUIRED IMPORTED_TARGET openssl)
    pkg_check_modules(_sys_zlib REQUIRED IMPORTED_TARGET zlib)
    pkg_check_modules(_sys_usb  REQUIRED IMPORTED_TARGET libusb-1.0)
    pkg_check_modules(_sys_avahi         IMPORTED_TARGET avahi-client)
    pkg_check_modules(_sys_png           IMPORTED_TARGET libpng)
    pkg_check_modules(_sys_jpeg          IMPORTED_TARGET libjpeg)

    # -------------------------------------------------------------------------
    # CUPS  (static archive)
    # -------------------------------------------------------------------------
    set(_cups_install   "${CMAKE_BINARY_DIR}/cups-prefix/install")
    set(_cups_pkgconfig "${_cups_install}/lib/pkgconfig")
    set(_cups_lib       "${_cups_install}/lib/libcups.a")

    file(MAKE_DIRECTORY "${_cups_install}/include"
                        "${_cups_install}/lib/pkgconfig"
                        "${_cups_install}/bin"
                        "${_cups_install}/etc/rc.d")

    set(_cups_flags
            --prefix=${_cups_install}
            --with-rcdir=${_cups_install}/etc/rc.d
            --libdir=${_cups_install}/lib
            --disable-shared
            --disable-gssapi
            --with-tls=openssl
            ${_host_arg}
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

    # install-libs/install-headers do not install cups-config or cups.pc, both
    # of which PAPPL's configure needs — copy them in explicitly.
    ExternalProject_Add(cups_proj
            SOURCE_DIR        "${cups_src_SOURCE_DIR}"
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
                              CC=${CMAKE_C_COMPILER}
                              CXX=${CMAKE_CXX_COMPILER}
                              PKG_CONFIG=${PKG_CONFIG_EXECUTABLE}
                              CFLAGS=${_cups_cflags}
                              ./configure ${_cups_flags}
            BUILD_COMMAND     make -j${NPROC} libs
            INSTALL_COMMAND   sh -c "make install-libs install-headers && \
                                     install -m0755 cups-config '${_cups_install}/bin/cups-config' && \
                                     install -m0644 cups.pc '${_cups_pkgconfig}/cups.pc'"
            BUILD_IN_SOURCE   1
            BUILD_BYPRODUCTS  "${_cups_lib}"
    )

    # Transitive deps live on the archive target's own INTERFACE_LINK_LIBRARIES
    # so they are emitted AFTER libcups.a on the link line — required for the
    # static archive's references (ssl/avahi/m) to resolve under --as-needed.
    set(_cups_deps PkgConfig::_sys_ssl PkgConfig::_sys_zlib Threads::Threads m ${CMAKE_DL_LIBS})
    if(_sys_avahi_FOUND)
        list(APPEND _cups_deps PkgConfig::_sys_avahi)
    endif()

    add_library(cups_external STATIC IMPORTED)
    set_target_properties(cups_external PROPERTIES
            IMPORTED_LOCATION             "${_cups_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_cups_install}/include"
            INTERFACE_LINK_LIBRARIES      "${_cups_deps}"
    )
    add_dependencies(cups_external cups_proj)

    add_library(CUPS::cups INTERFACE IMPORTED)
    set_target_properties(CUPS::cups PROPERTIES
            INTERFACE_LINK_LIBRARIES "cups_external"
    )

    # -------------------------------------------------------------------------
    # PAPPL  (static archive, depends on CUPS)
    # -------------------------------------------------------------------------
    set(_pappl_install "${CMAKE_BINARY_DIR}/pappl-prefix/install")
    set(_pappl_lib     "${_pappl_install}/lib/libpappl.a")

    file(MAKE_DIRECTORY "${_pappl_install}/include" "${_pappl_install}/lib")

    set(_pappl_flags
            --prefix=${_pappl_install}
            --libdir=${_pappl_install}/lib
            --disable-shared
            ${_host_arg}
    )
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

    # PAPPL locates CUPS via cups-config (placed in the CUPS prefix bin above);
    # put it on PATH. Build/install only the 'pappl' subdir — the top-level
    # target also builds the testsuite, whose executables fail to link the
    # static libcups.a (fmod/-lm ordering); we only need the library.
    ExternalProject_Add(pappl_proj
            SOURCE_DIR        "${pappl_src_SOURCE_DIR}"
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
                              "PATH=${_cups_install}/bin:$ENV{PATH}"
                              CC=${CMAKE_C_COMPILER}
                              CXX=${CMAKE_CXX_COMPILER}
                              PKG_CONFIG=${PKG_CONFIG_EXECUTABLE}
                              "PKG_CONFIG_PATH=${_cups_pkgconfig}"
                              CFLAGS=${_pappl_cflags}
                              ./configure ${_pappl_flags}
            BUILD_COMMAND     ${CMAKE_COMMAND} -E env
                              "PATH=${_cups_install}/bin:$ENV{PATH}"
                              make -j${NPROC} -C pappl all
            INSTALL_COMMAND   make -C pappl install
            BUILD_IN_SOURCE   1
            BUILD_BYPRODUCTS  "${_pappl_lib}"
    )
    add_dependencies(pappl_proj cups_proj)

    # libpappl.a depends on libcups.a (cups_external, which carries the CUPS
    # system deps) plus PAPPL's own deps. All emitted after libpappl.a.
    set(_pappl_deps cups_external PkgConfig::_sys_usb PkgConfig::_sys_zlib Threads::Threads m ${CMAKE_DL_LIBS})
    if(_sys_png_FOUND)
        list(APPEND _pappl_deps PkgConfig::_sys_png)
    endif()
    if(_sys_jpeg_FOUND)
        list(APPEND _pappl_deps PkgConfig::_sys_jpeg)
    endif()

    add_library(pappl_external STATIC IMPORTED)
    set_target_properties(pappl_external PROPERTIES
            IMPORTED_LOCATION             "${_pappl_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_pappl_install}/include"
            INTERFACE_LINK_LIBRARIES      "${_pappl_deps}"
    )
    add_dependencies(pappl_external pappl_proj)

    add_library(PAPPL::pappl INTERFACE IMPORTED)
    set_target_properties(PAPPL::pappl PROPERTIES
            INTERFACE_LINK_LIBRARIES "pappl_external"
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
