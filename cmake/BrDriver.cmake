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

# br_add_driver(<name> SOURCES <src>... [INCLUDE_DIRS <dir>...])
#
# Makes the library <name> with the alias drivers::<name>. Its public headers are in
# ${PROJECT_SOURCE_DIR}/include. The target goes into the global property
# BR_THERMAL_DRIVERS, which src/CMakeLists.txt links into br_thermal.
function(br_add_driver name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;INCLUDE_DIRS")

    add_library(${name})
    add_library(drivers::${name} ALIAS ${name})
    target_sources(${name} PRIVATE ${ARG_SOURCES})
    target_link_libraries(${name} PUBLIC PAPPL::pappl CUPS::cups drivers::utils)
    target_compile_features(${name} PUBLIC cxx_std_23)
    target_include_directories(${name}
            PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${ARG_INCLUDE_DIRS}
    )

    set_property(GLOBAL APPEND PROPERTY BR_THERMAL_DRIVERS drivers::${name})
endfunction()

# br_add_driver_tests(<name> SOURCES <src>...)
#
# Makes <name>_tests, links it with drivers::<name> and Catch2, and runs it after each
# build. A test failure thus fails the build.
function(br_add_driver_tests name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES")

    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Catch2.cmake)

    add_executable(${name}_tests ${ARG_SOURCES})
    target_compile_features(${name}_tests PRIVATE cxx_std_23)
    target_link_libraries(${name}_tests PRIVATE Catch2::Catch2WithMain drivers::${name})
    catch_discover_tests(${name}_tests)

    add_custom_command(TARGET ${name}_tests
            COMMENT "run tests"
            POST_BUILD
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION> --output-on-failure
    )
endfunction()
