# Sanitizer configuration for OatppBackend.
#
# Single user-facing switch: ENABLE_SANITIZERS (ON/OFF, default ON). The selected build configuration determines which sanitizer set is used: Debug /
# RelWithDebInfo                  -> AddressSanitizer + UndefinedBehaviorSanitizer DebugTSan / RelWithDebInfoTSan          -> ThreadSanitizer only
# (never mixed with ASan) DebugMSan / RelWithDebInfoMSan          -> MemorySanitizer only (Clang only) Release / MinSizeRel                    ->
# none, always (policy: sanitizers are a development aid, never shipped) ENABLE_SANITIZERS=OFF -> none everywhere.
#
# Generator model: this project uses single-configuration generators only (Unix Makefiles, Ninja). Each preset selects exactly one CMAKE_BUILD_TYPE,
# so the custom types DebugTSan/DebugMSan/RelWithDebInfoTSan below get explicit base flags (CMake supplies none for custom names). Multi-configuration
# generators (Visual Studio, Ninja Multi-Config) are not used; they would additionally need CMAKE_CONFIGURATION_TYPES entries and per-config output
# dirs, which is out of scope and intentionally not implemented here.
#
# Single source of truth: app_effective_sanitizers() computes the active set from (option + build type + compiler). Both app_apply_sanitizers() and
# the build_summary report consume it, so flags and reporting cannot disagree.
#
# NOTE: -fno-omit-frame-pointer is repeated here even though the Debug-family build-type flags also set it. The duplication is intentional (same
# value, last-wins, no conflict): this module stays self-sufficient so sanitizer stack traces never depend on which build-type flags happened to
# apply.
#
# Deliberately NOT enabled (evaluated per section "extra checks"): - Standalone LeakSanitizer: on Linux ASan already integrates leak detection; a
# separate -fsanitize=leak would be redundant/misleading and is not listed. - Extra UBSan subchecks (pointer-overflow etc.): plain `undefined` already
# includes the useful default checks (incl. vptr where supported); extras add noise without value for this codebase. - CFI: needs Clang + LTO +
# visibility discipline across static oatpp archives; impractical here. - HWASan: AArch64-only; this project targets x86_64 Linux. - TSan is never
# combined with ASan/UBSan (incompatible runtimes).

# --- Explicit base flags for the custom configurations. ---
# CMake provides defaults only for Debug/Release/RelWithDebInfo/MinSizeRel. These cache entries make DebugTSan/DebugMSan/RelWithDebInfoTSan real build
# types with sane standalone behavior. Per-target tuning in cmake/compiler.cmake (apply_app_build_type_flags) is authoritative for optimization/debug
# levels; the values below mirror the corresponding base type so a bare -DCMAKE_BUILD_TYPE=DebugTSan build behaves predictably.

foreach(_lang C CXX)
    if(NOT DEFINED CMAKE_${_lang}_FLAGS_DEBUGTSAN)
        set(CMAKE_${_lang}_FLAGS_DEBUGTSAN
            "-O0 -g3"
            CACHE STRING "Flags used by the ${_lang} compiler during DebugTSan builds.")
    endif()

    if(NOT DEFINED CMAKE_${_lang}_FLAGS_DEBUGMSAN)
        set(CMAKE_${_lang}_FLAGS_DEBUGMSAN
            "-O0 -g3"
            CACHE STRING "Flags used by the ${_lang} compiler during DebugMSan builds.")
    endif()

    if(NOT DEFINED CMAKE_${_lang}_FLAGS_RELWITHDEBINFOTSAN)
        set(CMAKE_${_lang}_FLAGS_RELWITHDEBINFOTSAN
            "-O2 -g -DNDEBUG"
            CACHE STRING "Flags used by the ${_lang} compiler during RelWithDebInfoTSan builds.")
    endif()

    if(NOT DEFINED CMAKE_${_lang}_FLAGS_RELWITHDEBINFOMSAN)
        set(CMAKE_${_lang}_FLAGS_RELWITHDEBINFOMSAN
            "-O2 -g -DNDEBUG"
            CACHE STRING "Flags used by the ${_lang} compiler during RelWithDebInfoMSan builds.")
    endif()
endforeach()

foreach(_kind EXE SHARED)
    if(NOT DEFINED CMAKE_${_kind}_LINKER_FLAGS_DEBUGTSAN)
        set(CMAKE_${_kind}_LINKER_FLAGS_DEBUGTSAN
            ""
            CACHE STRING "Linker flags during DebugTSan builds.")
    endif()

    if(NOT DEFINED CMAKE_${_kind}_LINKER_FLAGS_DEBUGMSAN)
        set(CMAKE_${_kind}_LINKER_FLAGS_DEBUGMSAN
            ""
            CACHE STRING "Linker flags during DebugMSan builds.")
    endif()

    if(NOT DEFINED CMAKE_${_kind}_LINKER_FLAGS_RELWITHDEBINFOTSAN)
        set(CMAKE_${_kind}_LINKER_FLAGS_RELWITHDEBINFOTSAN
            ""
            CACHE STRING "Linker flags during RelWithDebInfoTSan builds.")
    endif()

    if(NOT DEFINED CMAKE_${_kind}_LINKER_FLAGS_RELWITHDEBINFOMSAN)
        set(CMAKE_${_kind}_LINKER_FLAGS_RELWITHDEBINFOMSAN
            ""
            CACHE STRING "Linker flags during RelWithDebInfoMSan builds.")
    endif()
endforeach()

# Compute the effective sanitizer set for the current configuration. out - set to a ;-list of runtimes: "address;undefined", "thread", "memory", or
# empty. Empty also covers ENABLE_SANITIZERS=OFF, Release/MinSizeRel (policy), unknown build types, and MSVC configurations without sanitizer support.
function(app_effective_sanitizers out)
    set(_result "")

    if(ENABLE_SANITIZERS)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _bt)

        if(_bt STREQUAL "DEBUG" OR _bt STREQUAL "RELWITHDEBINFO")
            if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
                # MSVC supports ASan but not UBSan: degrade honestly to ASan only so the summary never claims an inactive sanitizer.
                set(_result "address")
            else()
                set(_result "address;undefined")
            endif()
        elseif(_bt STREQUAL "DEBUGTSAN" OR _bt STREQUAL "RELWITHDEBINFOTSAN")
            set(_result "thread")
        elseif(_bt STREQUAL "DEBUGMSAN" OR _bt STREQUAL "RELWITHDEBINFOMSAN")
            set(_result "memory")
        endif()
        # Release/MinSizeRel/unknown: intentionally empty (policy below).
    endif()

    set(${out}
        "${_result}"
        PARENT_SCOPE)
endfunction()

# Human-readable label for the effective set (used by build_summary).
function(app_sanitizers_label out)
    app_effective_sanitizers(_set)

    if(_set STREQUAL "address;undefined")
        set(${out}
            "AddressSanitizer, UndefinedBehaviorSanitizer"
            PARENT_SCOPE)
    elseif(_set STREQUAL "address")
        set(${out}
            "AddressSanitizer (UBSan unavailable on MSVC)"
            PARENT_SCOPE)
    elseif(_set STREQUAL "thread")
        set(${out}
            "ThreadSanitizer"
            PARENT_SCOPE)
    elseif(_set STREQUAL "memory")
        set(${out}
            "MemorySanitizer"
            PARENT_SCOPE)
    else()
        set(${out}
            "None"
            PARENT_SCOPE)
    endif()
endfunction()

# Apply the effective sanitizer flags to a target (compile + link). Must be called for every built target (app_core, app_server, app_tests). Sanitizer
# runtimes must reach the final link, so this adds both compile and link options; warning/optimization flags stay in cmake/compiler.cmake.
function(app_apply_sanitizers target)
    app_effective_sanitizers(_sanitizers)

    if(_sanitizers STREQUAL "")
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        if(_sanitizers STREQUAL "address")
            message(WARNING "UBSan is not supported on MSVC; applying AddressSanitizer only.")
            target_compile_options(${target} PRIVATE /fsanitize=address)
            # The MSVC driver forwards /fsanitize=address to the link step.
        else()
            message(FATAL_ERROR "Sanitizer set '${_sanitizers}' is not supported on MSVC "
                                "(only AddressSanitizer is available; TSan/MSan/UBSan are not).")
        endif()
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers are only supported on GCC/Clang (and MSVC AddressSanitizer); "
                            "compiler is '${CMAKE_CXX_COMPILER_ID}'. Set ENABLE_SANITIZERS=OFF.")
    endif()

    if(_sanitizers STREQUAL "address;undefined")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    elseif(_sanitizers STREQUAL "thread")
        # Never combined with ASan/UBSan: the effective set for TSan profiles contains only `thread`, so no ASan flags can leak in from elsewhere.
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    elseif(_sanitizers STREQUAL "memory")
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            message(
                FATAL_ERROR
                    "MemorySanitizer (${CMAKE_BUILD_TYPE}) requires Clang; current compiler is "
                    "'${CMAKE_CXX_COMPILER_ID}'. MSan-instrumented C++ standard "
                    "library and dependencies are also required for reliable results "
                    "— build them with -fsanitize=memory as well, otherwise findings "
                    "may be incomplete. Use a TSan or ASan profile instead, or set "
                    "ENABLE_SANITIZERS=OFF.")
        endif()

        message(WARNING "MemorySanitizer needs all linked C++ code (stdlib, dependencies) built "
                        "with -fsanitize=memory too; otherwise results may be incomplete.")

        target_compile_options(${target} PRIVATE -fsanitize=memory -fno-omit-frame-pointer -fno-optimize-sibling-calls)

        target_link_options(${target} PRIVATE -fsanitize=memory)
    else()
        message(FATAL_ERROR "Unknown sanitizer set '${_sanitizers}' (internal error).")
    endif()
endfunction()
