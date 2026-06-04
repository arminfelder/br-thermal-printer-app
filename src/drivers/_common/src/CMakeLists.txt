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

add_library(utils)

add_library(drivers::utils ALIAS utils)

target_sources(utils
        PRIVATE
        util.cpp
)

target_compile_features(utils PUBLIC cxx_std_23)

# util.cpp (and the public util.h) include pappl/* headers. Link PAPPL::pappl
# PUBLIC so the include dirs propagate and, when CUPS/PAPPL are built from
# source, utils builds after those archives are available.
target_link_libraries(utils PUBLIC PAPPL::pappl)

target_include_directories(utils
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)