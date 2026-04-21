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
        br_thermal
        VERSION 0.1
        DESCRIPTION "application for Brother Thermal printers"
        LANGUAGES C CXX
        #SPDX_LICENSE GPL-3.0-or-later
)

# ==============================================================================
# Build Reproducibility Configuration
# ==============================================================================
set(BUILD_RANDOM_SEED "thing3Aa1ush8alie3OhV0wei6Oa0quahtheoreech4EfaeBioXefawooZeshego")
add_compile_options(-frandom-seed="${BUILD_RANDOM_SEED}")

# ==============================================================================
# Processor Detection
# ==============================================================================
include(ProcessorCount)
ProcessorCount(NPROC)
if (NOT NPROC OR NPROC EQUAL 0)
    set(NPROC 1)
endif ()

# ==============================================================================
# Safety Compile Flags
# ==============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/SafetyFlags.cmake)

# ==============================================================================
# Dependencies
# ==============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/PapplCups.cmake)
pappl_cups_setup()

# ==============================================================================
# Install Paths
# ==============================================================================
include(GNUInstallDirs)

# ==============================================================================
# Build Configuration
# ==============================================================================
include_directories(include)

# ==============================================================================
# Main Targets
# ==============================================================================
add_subdirectory(src)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(apps)
    install(FILES dist/br-thermal.service
                  dist/br-thermal-autoadd.service
            DESTINATION lib/systemd/system)
    install(FILES dist/br-thermal.default
            DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/default
            RENAME br-thermal)
    install(FILES dist/60-br-thermal.rules
            DESTINATION lib/udev/rules.d)
    install(FILES man/br-thermal.8
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man8)
    install(PROGRAMS dist/br-thermal-udev
            DESTINATION lib/udev)
endif ()

# ==============================================================================
# Testing
# ==============================================================================
include(CTest)
if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif ()