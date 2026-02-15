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

# https://airbus-seclab.github.io/c-compiler-security/

# === Warning Configuration ===
set(WARNING_FLAGS
        # Basic warnings
        -Wall
        -Wextra
        -Wpedantic

        # Format warnings
        -Wformat=2
        -Wformat-overflow=2
        -Wformat-truncation=2
        -Wformat-security
        -Wformat-signedness

        # Memory/stack warnings
        -Wnull-dereference
        -Wstack-protector
        -Wtrampolines
        -Walloca
        -Wvla
        -Wstack-usage=1000000

        # Array/bounds warnings
        -Warray-bounds=2
        -Wstringop-overflow=4
        -Wshift-overflow=2
        -Wstrict-overflow=4

        # Type/conversion warnings
        -Wcast-qual
        -Wcast-align=strict
        -Wconversion
        -Warith-conversion

        # Logic/flow warnings
        -Wimplicit-fallthrough=3
        -Wlogical-op
        -Wduplicated-cond
        -Wduplicated-branches

        # Switch warnings
        -Wswitch-default
        -Wswitch-enum

        # Miscellaneous
        -Wshadow
        -Wundef
)

set(C_SPECIFIC_FLAGS
        "$<$<COMPILE_LANGUAGE:C>:-Wtraditional-conversion>"
        "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
)

# === Security Hardening ===
set(SECURITY_HARDENING_FLAGS
        -fstack-protector-strong
        -fstack-clash-protection
        -fPIE
)

# === Apply Configuration ===
add_compile_options(-Werror -O2)
add_compile_options(${WARNING_FLAGS})
add_compile_options(${C_SPECIFIC_FLAGS})
add_compile_options(${SECURITY_HARDENING_FLAGS})
add_compile_definitions(_FORTIFY_SOURCE=3)