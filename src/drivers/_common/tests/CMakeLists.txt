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

include(FetchContent)

if(NOT TARGET Catch2::Catch2)
    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.12.0
    )
    FetchContent_MakeAvailable(Catch2)
    include(Catch)
endif()

include(CTest)

add_executable(util_tests
        util.cpp
)

target_compile_features(util_tests PRIVATE cxx_std_23)

target_link_libraries(util_tests PRIVATE Catch2::Catch2WithMain PAPPL::pappl drivers::utils)

catch_discover_tests(util_tests)

add_custom_command(TARGET util_tests
        COMMENT "run tests"
        POST_BUILD
        WORKING_DIRECTORY ${utils_BINARY_DIR}/tests
        COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION> --output-on-failure
)