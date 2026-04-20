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
    if(TARGET Catch2)
        target_compile_options(Catch2 PRIVATE
                -Wno-error
                -Wno-switch-enum
        )
    endif()
endif()

include(CTest)

add_executable(pte550w_tests
        commands.cpp
)

target_compile_features(pte550w_tests PRIVATE cxx_std_26)

target_link_libraries(pte550w_tests
        PRIVATE
        Catch2::Catch2WithMain
        CUPS::cups
        PAPPL::pappl
        drivers::utils
        drivers::pte550w
)

catch_discover_tests(pte550w_tests)

add_custom_command(TARGET pte550w_tests
        COMMENT "run tests"
        POST_BUILD
        WORKING_DIRECTORY ${pte550w_BINARY_DIR}/tests
        COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION> --output-on-failure
)
