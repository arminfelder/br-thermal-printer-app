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

set(MP_UNITS_VERSION 2.5.0)

function(mp_units_setup)
    if (TARGET mp-units::mp-units)
        return()
    endif ()

    # Header-only. Contracts set to NONE so the library does not pull gsl-lite
    # or ms-gsl; Debian trixie packages neither.
    set(MP_UNITS_API_CONTRACTS NONE CACHE STRING "" FORCE)
    set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)
    set(MP_UNITS_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

    find_package(mp-units ${MP_UNITS_VERSION} QUIET)
    if (mp-units_FOUND)
        return()
    endif ()

    include(FetchContent)
    # SYSTEM marks the interface include dirs as system headers. Without it the
    # global -Werror warning set (-Wconversion, -Wold-style-cast,
    # -Wunsafe-buffer-usage, ...) fires inside the mp-units headers.
    FetchContent_Declare(
            mp-units
            GIT_REPOSITORY https://github.com/mpusz/mp-units.git
            GIT_TAG v${MP_UNITS_VERSION}
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR src
            SYSTEM
    )
    FetchContent_MakeAvailable(mp-units)
endfunction()
