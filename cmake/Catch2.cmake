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

include_guard(GLOBAL)

# The one place that gets Catch2. Each tests/CMakeLists.txt includes this file.
#
# SYSTEM: the Catch2 headers give no warnings in the test sources of this project.
# -Wno-error: the Catch2 sources do not obey the warning set of this project.
include(FetchContent)
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.12.0
        SYSTEM
)
FetchContent_MakeAvailable(Catch2)

foreach (_catch2_target Catch2 Catch2WithMain)
    if (TARGET ${_catch2_target})
        target_compile_options(${_catch2_target} PRIVATE -Wno-error -Wno-switch-enum)
    endif ()
endforeach ()

# include(Catch) finds the module only in the directory that fetched Catch2, thus
# include it by path. catch_discover_tests is global after that.
FetchContent_GetProperties(Catch2)
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
