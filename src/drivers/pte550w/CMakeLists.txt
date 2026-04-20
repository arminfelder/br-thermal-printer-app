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

project(
        pte550w
        VERSION 0.1
        DESCRIPTION "PT-E550W / PT-P750W / PT-P710BT raster driver"
        LANGUAGES CXX
)

# ==============================================================================
# Dependencies
# ==============================================================================
if(NOT TARGET PAPPL::pappl)
    include(../../../cmake/PapplCups.cmake)
    pappl_cups_setup()
endif()

# ==============================================================================
# Build Configuration
# ==============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)

# ==============================================================================
# Main Targets
# ==============================================================================
add_subdirectory(src)

# ==============================================================================
# Testing
# ==============================================================================
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
