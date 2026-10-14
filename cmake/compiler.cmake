# Compiler configuration for OatppBackend.
#
# Provides: apply_app_warnings(<target>) - strict warnings for our own code (never third-party headers: linked dependencies expose SYSTEM includes, so
# these flags only diagnose our sources). GNU/Clang get the full set below; MSVC gets /W4. apply_app_build_type_flags(<target>) - per-config
# optimization / debug / size flags for Debug, RelWithDebInfo, Release, MinSizeRel plus the sanitizer-profile types
# (DebugTSan/DebugMSan/RelWithDebInfoTSan/ RelWithDebInfoMSan), which mirror their base type's tuning here; sanitizer runtimes themselves live in
# cmake/sanitizers.cmake. NOTE on sizes: do NOT expect Debug > RelWithDebInfo on disk. -O2 inlining duplicates (sanitizer-instrumented) code at every
# inline site and each copy gets full debug-info records, so RelWithDebInfo binaries are routinely LARGER than Debug ones (verified with sanitizers
# off too, and identically in the static archives, proving a compile-stage effect, not a linker misconfiguration). What holds is: debug/sanitizer
# builds >> stripped Release/MinSizeRel. apply_app_hardening(<target>) - production hardening for Release/MinSizeRel only (see below for why
# RelWithDebInfo is excluded).
#
# Sanitizers are NOT handled here; see cmake/sanitizers.cmake (ENABLE_SANITIZERS + app_apply_sanitizers).

function(apply_app_warnings target)
    target_compile_options(
        ${target}
        PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:
                # Base sets: questionable constructs, sloppy conformance.
                -Wall # _unused variables, suspicious control flow, implicit pitfalls.
                -Wextra # _unused parameters, missing initializers, sign compare, etc.
                -Wpedantic # _strict ISO C++: reject GNU extensions leaking into our code.
                # Unused code: catch dead stores and dead statics early.
                -Wunused-value # _computed expression result silently discarded.
                -Wunused-variable # _declared but never referenced variable.
                # -Wno-unused-parameter keeps overrides/callbacks with fixed signatures (oatpp handlers, listeners) warning-free.
                -Wno-unused-parameter
                -Wunused-function # _file-static function never called.
                -Wunused-but-set-variable # _assigned but never read: logic bug smell.
                # Type safety: silent conversions are a top source of truncation bugs.
                -Wconversion # _implicit conversions that may alter a value.
                -Wsign-conversion # _signed/unsigned mixing: wrap-around risk.
                -Wfloat-conversion # _narrowing float/double conversions.
                -Wdouble-promotion # _float promoted to double: perf + precision smell.
                # Likely bugs: shadowing, null dereference, bad macros, aliasing.
                -Wshadow # _local shadows outer scope: classic wrong-variable bug.
                -Wnull-dereference # _GCC path analysis spotting null dereference.
                -Wundef # _#if on undefined macro (silently 0): typo guard.
                -Wstrict-aliasing=2 # _type-punning violating aliasing rules (UB).
                # Format strings: printf-family mismatches are crashes/leaks.
                -Wformat=2 # _strict format-string checking.
                -Wformat-overflow=2 # _compile-time sprintf buffer overflow detection.
                -Wformat-truncation=2 # _snprintf truncation detection.
                # C++ specifics: object lifetime and cast discipline.
                -Wnon-virtual-dtor # _public base dtors must be virtual (one local
                # suppression exists where a base is non-virtual by design).
                -Woverloaded-virtual # _derived method hiding a base virtual overload.
                -Wold-style-cast # _C casts bypass the type system; use named casts.
                # -Wno-c++20-extensions: stay C++17-clean without noise when a newer toolchain offers C++20 conveniences.
                -Wno-c++20-extensions
                # Reproducibility: __DATE__/__TIME__ embed build timestamps.
                -Wdate-time # _warn if sources embed build dates (none do).
                >
                $<$<CXX_COMPILER_ID:MSVC>:/W4>)
endfunction()

# Per-build-type flags. No Debug > RelWithDebInfo size order is promised (see the note above: -O2 inlining + debug info routinely makes RelWithDebInfo
# larger; what holds is debug/sanitizer builds >> stripped Release/MinSizeRel). Debug (O0+g3+frame pointer) RelWithDebInfo (O2+g, GC sections) Release
# (O3+LTO+GC sections+strip, balanced) MinSizeRel (Os+LTO+GC sections+strip, smallest) DebugTSan/DebugMSan mirror Debug;
# RelWithDebInfoTSan/RelWithDebInfoMSan mirror RelWithDebInfo.
function(apply_app_build_type_flags target)
    # --- Debug family: full debuggability, no optimization. ---
    # -O0 keeps variables live and code line-mappable; -g3 adds macro definitions for debugger macro expansion; -fno-omit-frame-pointer gives clean
    # backtraces (also required by sanitizer symbolization).
    target_compile_options(
        ${target}
        PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<OR:$<CONFIG:Debug>,$<CONFIG:DebugTSan>,$<CONFIG:DebugMSan>>>:-O0 -g3 -fno-omit-frame-pointer
                # One section per function/data item so the linker can discard unreferenced code (pairs with --gc-sections below).
                -ffunction-sections -fdata-sections>)

    target_compile_definitions(${target} PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugTSan>,$<CONFIG:DebugMSan>>:DEBUG>
                                                 $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugTSan>,$<CONFIG:DebugMSan>>:_DEBUG>)

    target_link_options(
        ${target}
        PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<OR:$<CONFIG:Debug>,$<CONFIG:DebugTSan>,$<CONFIG:DebugMSan>>>:
        # Export symbols for addr2line/backtrace symbolization.
        -rdynamic
        # Drop sections unreferenced after --ffunction-sections above.
        -Wl,--gc-sections>)

    # --- RelWithDebInfo family: optimized but debuggable (O2+g). ---
    # -O2 is the deployed optimization level; -g keeps usable symbols for post-deployment crash analysis. No stripping here by design.
    target_compile_options(
        ${target}
        PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:RelWithDebInfoTSan>,$<CONFIG:RelWithDebInfoMSan>>>:-O2 -g
                -ffunction-sections -fdata-sections>)

    target_link_options(
        ${target}
        PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:RelWithDebInfoTSan>,$<CONFIG:RelWithDebInfoMSan>>>:-Wl,--gc-sections>
    )

    # --- Release: balanced speed/size (O3+LTO+GC sections+strip). ---
    # -O3 for throughput; -flto enables cross-TU inlining and dead-code elimination at link time (validated with GNU ld on this toolchain); -s strips
    # symbols for size (use RelWithDebInfo when symbols matter). -fno-omit-frame-pointer is kept for production profiling support at a small code-size
    # cost.
    target_compile_options(${target} PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O3 -flto -ffunction-sections -fdata-sections
                                             -fno-omit-frame-pointer>)

    target_link_options(
        ${target} PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-flto -s -Wl,--gc-sections
        # Linker-level optimization pass (faster symbol resolution).
        -Wl,-O1>)

    # --- MinSizeRel: smallest possible output (Os+LTO+GC sections+strip). ---
    # NOTE: -Wl,--icf=all is intentionally omitted: GNU ld.bfd rejects it (gold/lld-only); --gc-sections already drops unreferenced sections.
    # -fno-unwind-tables shrinks EH metadata; C++ exception unwinding still works, only synchronous backtraces degrade (acceptable: MinSizeRel is not
    # a debugging configuration).
    target_compile_options(
        ${target}
        PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:MinSizeRel>>:-Os -flto -ffunction-sections -fdata-sections
                # Omit unwind tables for size; C++ exception unwinding still works (landing pads retained), only synchronous backtraces degrade.
                -fno-unwind-tables -fno-asynchronous-unwind-tables>)

    target_link_options(${target} PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:MinSizeRel>>:-flto -s -Wl,--gc-sections -Wl,-O1>)

    # MSVC optimization levels per config (best-effort parity).
    target_compile_options(${target} PRIVATE $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<OR:$<CONFIG:Debug>,$<CONFIG:DebugTSan>,$<CONFIG:DebugMSan>>>:/Od /Zi>)

    target_compile_options(
        ${target}
        PRIVATE $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:RelWithDebInfoTSan>,$<CONFIG:RelWithDebInfoMSan>>>:/O2 /Zi>)

    target_compile_options(${target} PRIVATE $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL>)

    target_compile_options(${target} PRIVATE $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:MinSizeRel>>:/O1>)
endfunction()

# Production hardening for Release/MinSizeRel on Linux GNU/Clang. Deliberately NOT applied to RelWithDebInfo: that type has sanitizer-enabled
# siblings, and -D_FORTIFY_SOURCE is a known-bad combination with AddressSanitizer (fortified wrappers fight ASan interceptors); keeping hardening on
# the always-sanitizer-free types avoids the interaction entirely. Deliberately NOT applied to Debug: -O0 cannot satisfy FORTIFY's optimization
# requirement, and debuggability outranks hardening there. Considered and rejected: -fstack-clash-protection / CET (-fcf-protection): arch- and
# hardware-specific, marginal benefit here; -fvisibility=hidden: no shared-library ABI surface in this project (static lib + executables).
function(apply_app_hardening target)
    target_compile_definitions(
        ${target} PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<PLATFORM_ID:Linux>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:_FORTIFY_SOURCE=3>)

    # NOTE: _FORTIFY_SOURCE=3 needs glibc >= 2.35 and -O1+ (both true here: glibc 2.43, -O3/-Os). Older deployment libcs would need level 2.
    target_compile_options(
        ${target}
        PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<PLATFORM_ID:Linux>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:
                # Strong stack canaries for functions with arrays/address-taken locals: cheap buffer-overflow tripwire.
                -fstack-protector-strong
                # Position-independent code so ASLR actually randomizes the binary.
                -fPIE>)

    target_link_options(
        ${target}
        PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<PLATFORM_ID:Linux>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:
        # Position-independent executable (pairs with -fPIE above).
        -pie
        # RELRO + immediate binding: GOT is read-only after load, PLT resolved eagerly; narrows GOT-overwrite attacks.
        -Wl,-z,RelRO,-z,Now>)
endfunction()
