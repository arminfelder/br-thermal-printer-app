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
#
# References:
#   https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html
#   https://airbus-seclab.github.io/c-compiler-security/

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

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

# Probe a linker flag; if supported, append it to LIST_VAR.
macro(try_add_link_flag LIST_VAR FLAG)
    string(MAKE_C_IDENTIFIER "HASLINK${FLAG}" _probe_var)
    check_linker_flag(CXX "${FLAG}" ${_probe_var})
    if (${_probe_var})
        list(APPEND ${LIST_VAR} "${FLAG}")
    endif ()
endmacro()

# ---------------------------------------------------------------------------
# Warning flags — shared by GCC and Clang
# ---------------------------------------------------------------------------
set(WARNING_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        # Both -Wformat and -Wformat=2 are needed: in Clang they are disjoint
        # sets; in GCC -Wformat=2 is a superset but specifying both is harmless.
        -Wformat
        -Wformat=2
        # Promote format-string-as-variable-without-args to error; enforced
        # by Arch Linux, Fedora, and Ubuntu for all distribution packages.
        -Werror=format-security
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
            -Wcast-align=strict
            -Wformat-signedness
            -Wstack-protector
            # Warns about nested-function trampolines that need executable stacks;
            # must pair with -Wl,-z,noexecstack to catch incompatibilities early.
            -Wtrampolines
            -Wlogical-op
            -Wduplicated-cond
            -Wduplicated-branches
            -Wuse-after-free=2
            # Counter Trojan Source (CVE-2021-42574): warn on any bidi control chars
            # in comments, strings, and identifiers. Skip this flag if your source
            # legitimately contains RTL-script comments (Arabic, Hebrew, etc.).
    )
    try_add_flag(WARNING_FLAGS -Wbidi-chars=any)   # GCC >= 12
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND WARNING_FLAGS
            -Wunsafe-buffer-usage
    )
endif ()

# ---------------------------------------------------------------------------
# Conditionally supported warnings (probed at configure time)
# ---------------------------------------------------------------------------
try_add_flag(WARNING_FLAGS -Wformat-overflow=2)
try_add_flag(WARNING_FLAGS -Wformat-truncation=2)
try_add_flag(WARNING_FLAGS -Wstringop-overflow=4)
try_add_flag(WARNING_FLAGS -Wstack-usage=1000000)
try_add_flag_or_fallback(WARNING_FLAGS -Warray-bounds=2 -Warray-bounds)
try_add_flag_or_fallback(WARNING_FLAGS -Wshift-overflow=2 -Wshift-overflow)
try_add_flag_or_fallback(WARNING_FLAGS -Wstrict-overflow=4 -Wstrict-overflow)
try_add_flag(WARNING_FLAGS -Wswitch-default)
try_add_flag(WARNING_FLAGS -Wswitch-enum)
try_add_flag(WARNING_FLAGS -Wold-style-cast)
try_add_flag(WARNING_FLAGS -Wuseless-cast)
try_add_flag(WARNING_FLAGS -Wzero-as-null-pointer-constant)

# ---------------------------------------------------------------------------
# C-only flags (guarded by generator expression)
# ---------------------------------------------------------------------------
set(C_SPECIFIC_FLAGS
        "$<$<COMPILE_LANGUAGE:C>:-Wtraditional-conversion>"
        "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
        # Treat obsolete pre-C99 constructs as errors (C only).
        # Prepares code for GCC 14 / Clang 15 which disable them by default.
        "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit>"
        "$<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>"
        "$<$<COMPILE_LANGUAGE:C>:-Werror=int-conversion>"
)

# ---------------------------------------------------------------------------
# Runtime hardening flags — architecture-independent
# ---------------------------------------------------------------------------
set(SECURITY_HARDENING_FLAGS
        -fstack-protector-strong      # GCC 4.9 / Clang 6.0; all arches
        -fPIE                         # required for ASLR; ~0% on 64-bit
        -D_GLIBCXX_ASSERTIONS         # libstdc++ bounds/null checks (C++ only)
)

# GCC >= 8 / Clang >= 11; probes required — Clang silently accepts but may
# not implement on all targets.
try_add_flag(SECURITY_HARDENING_FLAGS -fstack-clash-protection)

# GCC >= 12 / Clang >= 8; zero-init trivial automatic variables.
# NOTE: incompatible with Valgrind / MemorySanitizer — excluded from
# sanitizer builds below.
try_add_flag(SECURITY_HARDENING_FLAGS -ftrivial-auto-var-init=zero)

# GCC >= 7 / Clang >= 6; eliminates PLT trampolines. Combined with full
# RELRO (-Wl,-z,now) closes GOT/PLT indirect-call hijack surface on all arches.
try_add_flag(SECURITY_HARDENING_FLAGS -fno-plt)

# GCC >= 13 / Clang >= 16; improves FORTIFY_SOURCE and -fsanitize=bounds
# coverage by making the compiler respect declared sizes of trailing struct
# arrays. Code using the "struct hack" ([0] or [1] trailing arrays) may need
# porting to the C99 flexible array notation ([]).
try_add_flag(SECURITY_HARDENING_FLAGS -fstrict-flex-arrays=3)

# GCC >= 15 only; guarantees zero-init of padding bits in automatic variable
# initializers, closing an info-leak avenue from uninitialised padding.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    try_add_flag(SECURITY_HARDENING_FLAGS -fzero-init-padding-bits=all)
endif ()

# ---------------------------------------------------------------------------
# Runtime hardening flags — architecture-specific
# ---------------------------------------------------------------------------

# x86 / x86_64: Intel CET (Control-flow Enforcement Technology)
#   IBT  — indirect branch tracking (blocks JOP)
#   SHSTK — shadow stack (blocks ROP; requires Linux >= 6.6 + glibc >= 2.39)
#   Instructions are NOPs on CPUs without CET support.
#   Default in GCC >= 14; explicitly set here for older compilers.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i[3-6]86|AMD64")
    try_add_flag(SECURITY_HARDENING_FLAGS -fcf-protection=full)
endif ()

# AArch64: PAC + BTI (branch protection)
#   pac-ret  — signs return addresses (Armv8.3-A+)
#   bti      — branch target identification / landing pads (Armv8.5-A+)
#   Both occupy the HINT space → NOPs on non-supporting cores → fully
#   backward-compatible. Protection activates transparently when both CPU
#   and kernel support it (Linux >= 5.8 for BTI, >= 5.10 for PAC).
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    try_add_flag(SECURITY_HARDENING_FLAGS -mbranch-protection=standard)
endif ()

# RISC-V 64:
#   Zicfilp (landing pads) and Zicfiss (shadow stack) were ratified in 2024
#   but stable compiler flags (-mzicfilp / -mzicfiss) are not yet in
#   upstream GCC/Clang as of 2026. All architecture-agnostic hardening
#   (stack protector, RELRO, PIE, FORTIFY_SOURCE, trivial-auto-var-init,
#   no-plt) applies normally. Revisit when upstream flags land.

# Clang CFI (Control-Flow Integrity) — requires LTO + lld.
# Not auto-enabled; documented here for hardened release builds:
#   cmake -DCMAKE_CXX_FLAGS="-flto -fsanitize=cfi" \
#         -DCMAKE_EXE_LINKER_FLAGS="-flto -fsanitize=cfi -fuse-ld=lld"
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    check_cxx_compiler_flag("-fsanitize=cfi" HAS_FSANITIZE_CFI)
endif ()

# ---------------------------------------------------------------------------
# Linker hardening flags
# ---------------------------------------------------------------------------
# -pie                        Position-independent executable (ASLR)
# -z relro                    GOT + other data read-only after startup
# -z now                      Full RELRO: resolve all syms at load time;
#                             combined with -fno-plt eliminates PLT hijack
# -z noexecstack              Non-executable stack (NX); pairs with -Wtrampolines
# -z separate-code            Isolate code pages from data (binutils >= 2.31)
# --as-needed                 Omit unused libraries from the dynamic link set
# --no-copy-dt-needed-entries Prevent transitive DT_NEEDED symbol leakage
set(HARDENING_LINK_FLAGS
        -pie
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
)
try_add_link_flag(HARDENING_LINK_FLAGS -Wl,-z,separate-code)
try_add_link_flag(HARDENING_LINK_FLAGS -Wl,--as-needed)
try_add_link_flag(HARDENING_LINK_FLAGS -Wl,--no-copy-dt-needed-entries)

# ---------------------------------------------------------------------------
# Apply compile and link flags globally
# ---------------------------------------------------------------------------
add_compile_options(${WARNING_FLAGS})
add_compile_options(${C_SPECIFIC_FLAGS})
add_compile_options(${SECURITY_HARDENING_FLAGS})
add_link_options(${HARDENING_LINK_FLAGS})

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ENABLE_SANITIZERS "Enable runtime sanitizers for debug builds" OFF)
option(STATIC_LIBSTDCXX "Link libstdc++ and libgcc statically (for running on older distros)" OFF)

# ---------------------------------------------------------------------------
# _FORTIFY_SOURCE
#   - Requires -O1+; apply only to non-Debug builds.
#   - Must be prefixed with -U_FORTIFY_SOURCE to clear any distro-injected
#     default (e.g. Ubuntu 22.04 injects level 2).
#   - Incompatible with AddressSanitizer / MemorySanitizer / Valgrind:
#     excluded from sanitizer builds.
# ---------------------------------------------------------------------------
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ENABLE_SANITIZERS)
    add_compile_options(-U_FORTIFY_SOURCE)
    add_compile_definitions(_FORTIFY_SOURCE=3)
elseif (ENABLE_SANITIZERS)
    # Sanitizer builds: explicitly unset any distro default to avoid
    # false positives / false negatives.
    add_compile_options(-U_FORTIFY_SOURCE)
    add_compile_definitions(_FORTIFY_SOURCE=0)
endif ()

# ---------------------------------------------------------------------------
# Production-only hardening flags (non-Debug, non-sanitizer builds)
#   -fno-delete-null-pointer-checks  Retain null checks the optimizer would
#                                    otherwise elide as "unreachable".
#   -fno-strict-overflow             Define signed integer overflow as
#                                    two's-complement wrap (GCC: full since
#                                    8.5; Clang: alias for -fwrapv).
#   -fno-strict-aliasing             Disable type-based alias analysis;
#                                    matches Linux kernel practice.
# ---------------------------------------------------------------------------
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ENABLE_SANITIZERS)
    set(PRODUCTION_FLAGS "")
    try_add_flag(PRODUCTION_FLAGS -fno-delete-null-pointer-checks)
    try_add_flag(PRODUCTION_FLAGS -fno-strict-overflow)
    try_add_flag(PRODUCTION_FLAGS -fno-strict-aliasing)
    add_compile_options(${PRODUCTION_FLAGS})
endif ()

# ---------------------------------------------------------------------------
# Sanitizer profile (Debug builds only)
# ---------------------------------------------------------------------------
if (ENABLE_SANITIZERS)
    message(STATUS "[SafetyFlags] Sanitizers ENABLED — do not use this binary in production")
    set(SANITIZER_FLAGS
            -O1
            -g
            -fno-omit-frame-pointer
            # -ftrivial-auto-var-init=zero is incompatible with MemorySanitizer;
            # the flag was already added globally above but sanitizer toolchains
            # that report conflicts will error, prompting removal at that point.
            -fsanitize=address,undefined,leak
    )
    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(-fsanitize=address,undefined,leak)
endif ()

# ---------------------------------------------------------------------------
# Optimization level
# ---------------------------------------------------------------------------
if (ENABLE_SANITIZERS)
    # -O1 already set in SANITIZER_FLAGS above
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0)
else ()
    add_compile_options(-O2)
endif ()

# ---------------------------------------------------------------------------
# Static libstdc++/libgcc (cross-distro deployment)
# ---------------------------------------------------------------------------
if (STATIC_LIBSTDCXX)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        message(STATUS "[SafetyFlags] Linking libstdc++ and libgcc statically")
        add_link_options(-static-libstdc++ -static-libgcc)
    else ()
        message(WARNING "[SafetyFlags] STATIC_LIBSTDCXX is only supported with GCC")
    endif ()
endif ()
