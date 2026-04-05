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

include(CheckCXXCompilerFlag)

# ---------------------------------------------------------------------------
# Helper macros
# ---------------------------------------------------------------------------

# Probe FLAG; if supported, append it to LIST_VAR.
macro(try_add_flag LIST_VAR FLAG)
    string(MAKE_C_IDENTIFIER "HAS${FLAG}" _probe_var)
    check_cxx_compiler_flag("${FLAG}" ${_probe_var})
    if (${_probe_var})
        list(APPEND ${LIST_VAR} "${FLAG}")
    endif ()
endmacro()

# Try PREFERRED_FLAG first; if unsupported, fall back to FALLBACK_FLAG.
# Both are probed; whichever is available (preferred wins) is appended to LIST_VAR.
macro(try_add_flag_or_fallback LIST_VAR PREFERRED_FLAG FALLBACK_FLAG)
    string(MAKE_C_IDENTIFIER "HAS${PREFERRED_FLAG}" _pref_var)
    check_cxx_compiler_flag("${PREFERRED_FLAG}" ${_pref_var})
    if (${_pref_var})
        list(APPEND ${LIST_VAR} "${PREFERRED_FLAG}")
    else ()
        string(MAKE_C_IDENTIFIER "HAS${FALLBACK_FLAG}" _fall_var)
        check_cxx_compiler_flag("${FALLBACK_FLAG}" ${_fall_var})
        if (${_fall_var})
            list(APPEND ${LIST_VAR} "${FALLBACK_FLAG}")
        endif ()
    endif ()
endmacro()

# ---------------------------------------------------------------------------
# Warning flags shared by GCC and Clang
# ---------------------------------------------------------------------------
set(WARNING_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wformat=2
        -Wformat-security
        -Wnull-dereference
        -Wcast-qual
        -Wcast-align           # plain form: supported by both GCC and Clang
        -Wshadow
        -Wundef
        -Wdouble-promotion
        -Wmissing-declarations
        -Wmissing-field-initializers
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wimplicit-fallthrough
        -Wvla
        -Walloca
)

# ---------------------------------------------------------------------------
# Compiler-specific warning flags
# ---------------------------------------------------------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    list(APPEND WARNING_FLAGS
            -Wcast-align=strict       # GCC-only: stricter alignment checking
            -Wformat-signedness       # GCC-only
            -Wstack-protector         # GCC-only
            -Wtrampolines             # GCC-only: warns about nested function trampolines
            -Wlogical-op              # GCC-only: suspicious logical operator use
            -Wduplicated-cond         # GCC-only: duplicated conditions in if/else-if
            -Wduplicated-branches     # GCC-only: if/else branches with identical bodies
            -Wuse-after-free=2        # GCC-only
    )
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND WARNING_FLAGS
            -Wunsafe-buffer-usage     # Clang-only: unsafe pointer/array access patterns
    )
endif ()

# ---------------------------------------------------------------------------
# Conditionally supported warnings (probed at configure time)
# ---------------------------------------------------------------------------

# GCC >= 7 / not in Clang
try_add_flag(WARNING_FLAGS -Wformat-overflow=2)
try_add_flag(WARNING_FLAGS -Wformat-truncation=2)

# GCC-only
try_add_flag(WARNING_FLAGS -Wstringop-overflow=4)

# GCC-only: warn if a function uses more stack than the given limit
try_add_flag(WARNING_FLAGS -Wstack-usage=1000000)

# GCC supports =2, Clang only supports bare -Warray-bounds
try_add_flag_or_fallback(WARNING_FLAGS -Warray-bounds=2 -Warray-bounds)

# GCC supports =2, Clang supports bare -Wshift-overflow
try_add_flag_or_fallback(WARNING_FLAGS -Wshift-overflow=2 -Wshift-overflow)

# Both support bare form; =4 is GCC-only
try_add_flag_or_fallback(WARNING_FLAGS -Wstrict-overflow=4 -Wstrict-overflow)

# Supported by both GCC and Clang; probed to avoid issues on unusual toolchains
try_add_flag(WARNING_FLAGS -Wswitch-default)
try_add_flag(WARNING_FLAGS -Wswitch-enum)
try_add_flag(WARNING_FLAGS -Wold-style-cast)

# GCC-only: warns on casts to a type that are the same type (e.g. (int)(int)x)
try_add_flag(WARNING_FLAGS -Wuseless-cast)

# Supported by both
try_add_flag(WARNING_FLAGS -Wzero-as-null-pointer-constant)

# ---------------------------------------------------------------------------
# C-only flags (guarded by generator expression)
# ---------------------------------------------------------------------------
set(C_SPECIFIC_FLAGS
        "$<$<COMPILE_LANGUAGE:C>:-Wtraditional-conversion>"
        "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
)

# ---------------------------------------------------------------------------
# Hardening flags
# ---------------------------------------------------------------------------
set(SECURITY_HARDENING_FLAGS
        -fstack-protector-strong
        -fPIE
        -D_GLIBCXX_ASSERTIONS
)

# Supported by GCC >= 8 and Clang >= 7; probe to be safe
try_add_flag(SECURITY_HARDENING_FLAGS -fstack-clash-protection)

# Zero-initialise trivial automatic variables (GCC >= 12, Clang >= 8)
try_add_flag(SECURITY_HARDENING_FLAGS -ftrivial-auto-var-init=zero)

# Intel CET control-flow enforcement (x86 Linux, GCC >= 8, Clang >= 7)
# -fcf-protection=full enables both IBT (indirect branch tracking) and SHSTK (shadow stack)
try_add_flag(SECURITY_HARDENING_FLAGS -fcf-protection=full)

# Control-flow integrity: Clang-only
# Requires -flto and a compatible linker (lld recommended).
# To enable: add -flto -fsanitize=cfi to both compile and link options.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    check_cxx_compiler_flag("-fsanitize=cfi" HAS_FSANITIZE_CFI)
    # CFI is intentionally not auto-enabled here as it requires LTO setup;
    # left as a documented option for hardened release builds.
endif ()

# ---------------------------------------------------------------------------
# Linker hardening flags
# ---------------------------------------------------------------------------
# -pie:           Produces a Position Independent Executable (required for ASLR)
# -z relro:       Makes the GOT and other data read-only after startup
# -z now:         Resolves all symbols at load time (full RELRO), prevents GOT overwrites
# -z noexecstack: Marks the stack segment as non-executable
set(HARDENING_LINK_FLAGS
        -pie
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
)

# ---------------------------------------------------------------------------
# Apply all flags globally
# ---------------------------------------------------------------------------
add_compile_options(${WARNING_FLAGS})
add_compile_options(${C_SPECIFIC_FLAGS})
add_compile_options(${SECURITY_HARDENING_FLAGS})
add_link_options(${HARDENING_LINK_FLAGS})

# _FORTIFY_SOURCE requires at least -O1 to be effective.
# Guard against Debug builds where CMake injects -O0.
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(_FORTIFY_SOURCE=3)
endif ()

# ---------------------------------------------------------------------------
# Debug-sanitized profile
# ---------------------------------------------------------------------------
option(ENABLE_SANITIZERS "Enable runtime sanitizers for debug builds" OFF)
if (ENABLE_SANITIZERS)
    message(STATUS "[SafetyFlags] Sanitizers ENABLED — do not use this binary in production")
    set(SANITIZER_FLAGS
            -O1
            -g
            -fno-omit-frame-pointer
            -fsanitize=address,undefined,leak
    )
    set(SANITIZER_LINK_FLAGS
            -fsanitize=address,undefined,leak
    )
    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_LINK_FLAGS})
else ()
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0)
    endif ()
endif ()

# ---------------------------------------------------------------------------
# Common optimization level for non-sanitized builds
# ---------------------------------------------------------------------------
if (NOT ENABLE_SANITIZERS)
    add_compile_options(-O2)
endif ()