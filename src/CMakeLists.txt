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

cmake_minimum_required(VERSION 3.31)

add_library(br_thermal)

target_sources(br_thermal
        PRIVATE
        BrThermal.cpp
)

# On some systems you may also need to propagate runtime search path
set(PAPPL_LIB_DIR "${PAPPL_INSTALL_DIR}/lib")
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TARGET br_thermal PROPERTY INTERFACE_LINK_DIRECTORIES "${PAPPL_LIB_DIR}")
    set_property(TARGET br_thermal PROPERTY BUILD_RPATH "${PAPPL_LIB_DIR}")
endif ()


target_compile_features(br_thermal PUBLIC cxx_std_23)
target_link_libraries(br_thermal PUBLIC PAPPL::pappl CUPS::cups drivers::td2000 drivers::pte550w)

add_subdirectory(drivers)