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

include(${PROJECT_SOURCE_DIR}/cmake/Catch2.cmake)

add_executable(registry_tests
        registry.cpp
)
target_compile_features(registry_tests PRIVATE cxx_std_23)
target_link_libraries(registry_tests PRIVATE Catch2::Catch2WithMain br_thermal)
catch_discover_tests(registry_tests)

# The PAPPL callbacks of each driver, with a PAPPL printer and job and the fake device.
add_executable(pipeline_tests
        pipeline.cpp
)
target_compile_features(pipeline_tests PRIVATE cxx_std_23)
target_include_directories(pipeline_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/drivers/_common/tests)
target_link_libraries(pipeline_tests PRIVATE Catch2::Catch2WithMain br_thermal)
catch_discover_tests(pipeline_tests)
