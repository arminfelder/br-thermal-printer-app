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

add_library(pte550w)

add_library(drivers::pte550w ALIAS pte550w)

target_sources(pte550w
        PRIVATE
        pte550w.cpp
)

target_link_libraries(pte550w PUBLIC PAPPL::pappl CUPS::cups drivers::utils)

target_compile_features(pte550w PUBLIC cxx_std_23)

target_include_directories(pte550w
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
