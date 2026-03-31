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

# based on https://airbus-seclab.github.io/c-compiler-security/
# adds stricter flags like -Wunsafe-buffer-usage

include(CheckCXXCompilerFlag)

# === Warning Configuration ===
set(WARNING_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wformat=2
        -Wformat-security
        -Wnull-dereference
        -Wcast-qual
        -Wcast-align
        -Wshadow
        -Wundef
        -Wdouble-promotion
        -Wmissing-declarations
        -Wmissing-field-initializers
        -Wnon-virtual-dtor
        -Woverloaded-virtual
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    list(APPEND WARNING_FLAGS
            -Wformat-signedness
            -Wstack-protector
            -Walloca
            -Wvla
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND WARNING_FLAGS
            -Wvla
            -Walloca
    )
endif()

# Optional warnings, only enabled if supported by the compiler
check_cxx_compiler_flag("-Wformat-overflow=2" HAS_WFORMAT_OVERFLOW)
if(HAS_WFORMAT_OVERFLOW)
    list(APPEND WARNING_FLAGS -Wformat-overflow=2)
endif()

check_cxx_compiler_flag("-Wformat-truncation=2" HAS_WFORMAT_TRUNCATION)
if(HAS_WFORMAT_TRUNCATION)
    list(APPEND WARNING_FLAGS -Wformat-truncation=2)
endif()

check_cxx_compiler_flag("-Warray-bounds=2" HAS_WARRAY_BOUNDS)
if(HAS_WARRAY_BOUNDS)
    list(APPEND WARNING_FLAGS -Warray-bounds=2)
endif()

check_cxx_compiler_flag("-Wstringop-overflow=4" HAS_WSTRINGOP_OVERFLOW)
if(HAS_WSTRINGOP_OVERFLOW)
    list(APPEND WARNING_FLAGS -Wstringop-overflow=4)
endif()

check_cxx_compiler_flag("-Wshift-overflow=2" HAS_WSHIFT_OVERFLOW)
if(HAS_WSHIFT_OVERFLOW)
    list(APPEND WARNING_FLAGS -Wshift-overflow=2)
endif()

check_cxx_compiler_flag("-Wstrict-overflow=4" HAS_WSTRICT_OVERFLOW)
if(HAS_WSTRICT_OVERFLOW)
    list(APPEND WARNING_FLAGS -Wstrict-overflow=4)
endif()

check_cxx_compiler_flag("-Wstack-usage=1000000" HAS_WSTACK_USAGE)
if(HAS_WSTACK_USAGE)
    list(APPEND WARNING_FLAGS -Wstack-usage=1000000)
endif()

check_cxx_compiler_flag("-Wlogical-op" HAS_WLOGICAL_OP)
if(HAS_WLOGICAL_OP)
    list(APPEND WARNING_FLAGS -Wlogical-op)
endif()

check_cxx_compiler_flag("-Wduplicated-cond" HAS_WDUPLICATED_COND)
if(HAS_WDUPLICATED_COND)
    list(APPEND WARNING_FLAGS -Wduplicated-cond)
endif()

check_cxx_compiler_flag("-Wduplicated-branches" HAS_WDUPLICATED_BRANCHES)
if(HAS_WDUPLICATED_BRANCHES)
    list(APPEND WARNING_FLAGS -Wduplicated-branches)
endif()

check_cxx_compiler_flag("-Wswitch-default" HAS_WSWITCH_DEFAULT)
if(HAS_WSWITCH_DEFAULT)
    list(APPEND WARNING_FLAGS -Wswitch-default)
endif()

check_cxx_compiler_flag("-Wswitch-enum" HAS_WSWITCH_ENUM)
if(HAS_WSWITCH_ENUM)
    list(APPEND WARNING_FLAGS -Wswitch-enum)
endif()

check_cxx_compiler_flag("-Wtrampolines" HAS_WTRAMPOLINES)
if(HAS_WTRAMPOLINES)
    list(APPEND WARNING_FLAGS -Wtrampolines)
endif()

check_cxx_compiler_flag("-Wunsafe-buffer-usage" HAS_WUNSAFE_BUFFER_USAGE)
if(HAS_WUNSAFE_BUFFER_USAGE)
    list(APPEND WARNING_FLAGS -Wunsafe-buffer-usage)
endif()

check_cxx_compiler_flag("-Wuse-after-free=2" HAS_WUSE_AFTER_FREE)
if(HAS_WUSE_AFTER_FREE)
    list(APPEND WARNING_FLAGS -Wuse-after-free=2)
endif()

check_cxx_compiler_flag("-Wold-style-cast" HAS_WOLD_STYLE_CAST)
if(HAS_WOLD_STYLE_CAST)
    list(APPEND WARNING_FLAGS -Wold-style-cast)
endif()

check_cxx_compiler_flag("-Wuseless-cast" HAS_WUSELESS_CAST)
if(HAS_WUSELESS_CAST)
    list(APPEND WARNING_FLAGS -Wuseless-cast)
endif()

check_cxx_compiler_flag("-Wzero-as-null-pointer-constant" HAS_WZERO_AS_NULL_POINTER_CONSTANT)
if(HAS_WZERO_AS_NULL_POINTER_CONSTANT)
    list(APPEND WARNING_FLAGS -Wzero-as-null-pointer-constant)
endif()

set(C_SPECIFIC_FLAGS
        "$<$<COMPILE_LANGUAGE:C>:-Wtraditional-conversion>"
        "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
)

# === Hardening Flags ===
set(SECURITY_HARDENING_FLAGS
        -fstack-protector-strong
        -fstack-clash-protection
        -fPIE
        -D_GLIBCXX_ASSERTIONS
)

check_cxx_compiler_flag("-fstack-clash-protection" HAS_FSTACK_CLASH_PROTECTION)
if(HAS_FSTACK_CLASH_PROTECTION)
    list(APPEND SECURITY_HARDENING_FLAGS -fstack-clash-protection)
endif()

# === Release-Hardened Profile ===
add_compile_options(${WARNING_FLAGS})
add_compile_options(${C_SPECIFIC_FLAGS})
add_compile_options(${SECURITY_HARDENING_FLAGS})
add_compile_definitions(_FORTIFY_SOURCE=3)

# === Debug-Sanitized Profile ===
option(ENABLE_SANITIZERS "Enable runtime sanitizers for debug builds" OFF)

if(ENABLE_SANITIZERS)
    add_compile_options(
            -O1
            -g
            -fno-omit-frame-pointer
            -fsanitize=address,undefined
    )
    add_link_options(
            -fsanitize=address,undefined
    )
endif()

# === Common Optimization Level for non-sanitized builds ===
if(NOT ENABLE_SANITIZERS)
    add_compile_options(-O2)
endif()