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

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(Catch2)
if(TARGET Catch2)
    target_compile_options(Catch2 PRIVATE
            -Wno-error
            -Wno-switch-enum
    )
endif()
#
#add_executable(br_thermal_tests
#        util.cpp
#        td2000.cpp
#        td2000.cpp
#)
#
#target_compile_features(br_thermal_tests PRIVATE cxx_std_26)
#
## If your project defines a library target, link it here.
## (This keeps the test setup working even before you refactor src into a library.)
#if(TARGET br_thermal)
#    target_link_libraries(br_thermal_tests PRIVATE br_thermal)
#elseif(TARGET br_thermal_lib)
#    target_link_libraries(br_thermal_tests PRIVATE br_thermal_lib)
#endif()
#
#target_link_libraries(br_thermal_tests PRIVATE Catch2::Catch2WithMain)
#
#include(Catch)
#catch_discover_tests(br_thermal_tests)
