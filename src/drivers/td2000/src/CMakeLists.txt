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

add_library(td2000)

add_library(drivers::td2000 ALIAS td2000)

target_sources(td2000
        PRIVATE
        td2000.cpp
)

target_link_libraries(td2000 drivers::utils)

target_compile_features(td2000 PUBLIC cxx_std_26)

target_include_directories(td2000
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_include_directories(td2000 PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/../data/generated"
)