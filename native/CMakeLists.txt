cmake_minimum_required(VERSION 3.22.1)

project("itantra-host" CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The host entry point: every [H] test in the implementation plan runs from
# here. It deliberately does NOT define its own source list — it reuses the
# one the Android build uses, so a host test exercises the same translation
# units that end up in libitantra-native.so.
#
# The Android build under android-native/app/src/main/cpp/ is untouched by
# this file and remains the AGP entry point.
set(ITANTRA_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../android-native/app/src/main/cpp")
get_filename_component(ITANTRA_CPP_DIR "${ITANTRA_CPP_DIR}" ABSOLUTE)

if(NOT EXISTS "${ITANTRA_CPP_DIR}/sources.cmake")
    message(FATAL_ERROR
            "Cannot find the shared source list at ${ITANTRA_CPP_DIR}/sources.cmake. "
            "The host build reuses the Android build's sources; it does not keep its own.")
endif()

# Defines ITANTRA_CORE_DIR (native/src), ITANTRA_CORE_SOURCES, ITANTRA_JNI_SOURCES.
include("${ITANTRA_CPP_DIR}/sources.cmake")

# ITANTRA_JNI_SOURCES is intentionally not used here. jni.h and android/log.h
# are not available on the host, and the JNI layer is a thin adapter with no
# logic of its own to test (handoff: "JNI BOUNDARY" — coarse, one call per
# clause).

if(MSVC)
    set(ITANTRA_HOST_WARNINGS /W4 /WX /permissive-)
else()
    set(ITANTRA_HOST_WARNINGS -Wall -Wextra -Wconversion -Wsign-conversion -Werror)
endif()

# ---------------------------------------------------------------------------
# C-04 — no floating point in the deterministic core, as a BUILD STEP.
#
# validation-benchmark-contract §5.1: "C-04 is automatable as a build step.
# Make it one." itantra-core depends on the check, so no core library and no
# test binary can be built while a violation exists.
# ---------------------------------------------------------------------------
add_executable(itantra-c04-check tools/c04_no_float.cpp)
target_compile_options(itantra-c04-check PRIVATE ${ITANTRA_HOST_WARNINGS})

file(GLOB_RECURSE ITANTRA_C04_FILES CONFIGURE_DEPENDS
        "${ITANTRA_CORE_DIR}/*.h"
        "${ITANTRA_CORE_DIR}/*.c"
        "${ITANTRA_CORE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/unit/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/unit/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/golden/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/golden/*.cpp")
list(APPEND ITANTRA_C04_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test/itest.h")

# A core or vendored source outside native/src would silently escape the check.
foreach(core_source IN LISTS ITANTRA_CORE_SOURCES ITANTRA_THIRD_PARTY_SOURCES)
    if(NOT core_source IN_LIST ITANTRA_C04_FILES)
        message(FATAL_ERROR "C-04: core source ${core_source} is not covered by the "
                            "no-float check. Core sources must live under ${ITANTRA_CORE_DIR}.")
    endif()
endforeach()

set(ITANTRA_C04_STAMP "${CMAKE_CURRENT_BINARY_DIR}/c04.stamp")
add_custom_command(
        OUTPUT "${ITANTRA_C04_STAMP}"
        COMMAND itantra-c04-check --self-test
        COMMAND itantra-c04-check ${ITANTRA_C04_FILES}
        COMMAND "${CMAKE_COMMAND}" -E touch "${ITANTRA_C04_STAMP}"
        DEPENDS itantra-c04-check ${ITANTRA_C04_FILES}
        COMMENT "C-04: checking the deterministic core for floating point"
        VERBATIM)
add_custom_target(itantra-c04 ALL DEPENDS "${ITANTRA_C04_STAMP}")

# ---------------------------------------------------------------------------
# Portable core — the same translation units as libitantra-native.so.
# ---------------------------------------------------------------------------
# Vendored third-party code (sources.cmake), not held to the project's
# warnings-as-errors flags. Same translation units as the Android library.
add_library(itantra-third-party STATIC ${ITANTRA_THIRD_PARTY_SOURCES})
target_include_directories(itantra-third-party PUBLIC "${ITANTRA_CORE_DIR}")
add_dependencies(itantra-third-party itantra-c04)

add_library(itantra-core STATIC ${ITANTRA_CORE_SOURCES})
target_include_directories(itantra-core PUBLIC "${ITANTRA_CORE_DIR}")
target_compile_options(itantra-core PRIVATE ${ITANTRA_HOST_WARNINGS})
target_link_libraries(itantra-core PUBLIC itantra-third-party)
add_dependencies(itantra-core itantra-c04)

# ---------------------------------------------------------------------------
# ITANTRA_DISABLE_AEAD — packet §6.10.2, implementation plan Phase 4.
#
# A diagnostic, not a staging mechanism: with it, sealed payloads are the
# plaintext payload, so a C-01 failure can be pinned on the payload or the
# crypto. Debug builds only. Two independent guards keep it out of release:
#   - here: refused for single-config release build types, and applied only
#     to the Debug configuration of multi-config generators
#   - crypto/aead.h: #error whenever the flag meets NDEBUG (contract C-43)
# ---------------------------------------------------------------------------
option(ITANTRA_DISABLE_AEAD "Debug-only diagnostic: bypass AEAD (packet 6.10.2)" OFF)
if(ITANTRA_DISABLE_AEAD)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        message(FATAL_ERROR "ITANTRA_DISABLE_AEAD is debug-only and must not be set for a "
                            "${CMAKE_BUILD_TYPE} build (packet §6.10.2, contract C-43).")
    endif()
    target_compile_definitions(itantra-core PUBLIC $<$<CONFIG:Debug>:ITANTRA_DISABLE_AEAD>)
    message(WARNING "ITANTRA_DISABLE_AEAD is ON: Debug builds do not encrypt or authenticate.")
endif()

# The same core compiled with the bypass, for the plan's "ITANTRA_DISABLE_AEAD
# bypasses cleanly in debug" test. Debug configuration only; in any other
# configuration it is the normal core and the test reports itself skipped.
add_library(itantra-core-aead-bypass STATIC EXCLUDE_FROM_ALL ${ITANTRA_CORE_SOURCES})
target_include_directories(itantra-core-aead-bypass PUBLIC "${ITANTRA_CORE_DIR}")
target_compile_options(itantra-core-aead-bypass PRIVATE ${ITANTRA_HOST_WARNINGS})
target_compile_definitions(itantra-core-aead-bypass PUBLIC $<$<CONFIG:Debug>:ITANTRA_DISABLE_AEAD>)
target_link_libraries(itantra-core-aead-bypass PUBLIC itantra-third-party)
add_dependencies(itantra-core-aead-bypass itantra-c04)

# ---------------------------------------------------------------------------
# [H] tests. Run with:  ctest --test-dir native/build -C Debug
# ---------------------------------------------------------------------------
enable_testing()

add_test(NAME C-04 COMMAND itantra-c04-check ${ITANTRA_C04_FILES})
add_test(NAME C-04.self-test COMMAND itantra-c04-check --self-test)

function(itantra_host_test test_name target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE itantra-core)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test")
    target_compile_options(${target} PRIVATE ${ITANTRA_HOST_WARNINGS})
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

itantra_host_test(unit.bitio bitio_test test/unit/bitio_test.cpp)
itantra_host_test(unit.hash  hash_test  test/unit/hash_test.cpp)
itantra_host_test(unit.coder coder_test test/unit/coder_test.cpp)
itantra_host_test(unit.coder.fuzz coder_fuzz_test test/unit/coder_fuzz_test.cpp)

# Phase 2: "a symbol with p == 0 is a programming error → assert, not garbage".
# An assert ends the process, so each contract violation runs in a process of
# its own and passes only if THAT assert's message appears. NOT-ABORTED means
# the violation returned instead of asserting. In an NDEBUG configuration the
# binary reports the case skipped, and coder_test's release_* cases check that
# the violation is rejected with nothing written.
function(itantra_expect_assert case message)
    add_test(NAME unit.coder.assert.${case} COMMAND coder_test --expect-assert ${case})
    set_tests_properties(unit.coder.assert.${case} PROPERTIES
            PASS_REGULAR_EXPRESSION "${message}"
            FAIL_REGULAR_EXPRESSION "NOT-ABORTED|UNKNOWN-CASE"
            SKIP_REGULAR_EXPRESSION "ASSERTIONS-DISABLED"
            TIMEOUT 60)
endfunction()

itantra_expect_assert(encode-zero-frequency  "zero-probability or malformed symbol range")
itantra_expect_assert(encode-out-of-alphabet "zero-probability or malformed symbol range")
itantra_expect_assert(encode-empty-model     "zero-probability or malformed symbol range")
itantra_expect_assert(encode-after-finish    "encode after finish")
itantra_expect_assert(decode-empty-model     "model total out of range")
itantra_expect_assert(decode-broken-model    "model find broke its contract")

# ---------------------------------------------------------------------------
# Phase 3 — payload assembly, parsing, golden vectors
# ---------------------------------------------------------------------------
itantra_host_test(unit.metadata metadata_test test/unit/metadata_test.cpp)
itantra_host_test(unit.assemble assemble_test test/unit/assemble_test.cpp)

# packet §5: "hash_present set but hash invalid — programming error; assert in
# debug". Same pattern as the coder assert cases above.
add_test(NAME unit.assemble.assert.hash-too-wide
         COMMAND assemble_test --expect-assert assemble-hash-too-wide)
set_tests_properties(unit.assemble.assert.hash-too-wide PROPERTIES
        PASS_REGULAR_EXPRESSION "hash_present set but context_hash invalid"
        FAIL_REGULAR_EXPRESSION "NOT-ABORTED|UNKNOWN-CASE"
        SKIP_REGULAR_EXPRESSION "ASSERTIONS-DISABLED"
        TIMEOUT 60)

# The generator is built so it keeps compiling, but NEVER run by the build or
# by CTest: test/golden/vectors.bin is frozen (contract §2.2, §7.2). It refuses
# to overwrite an existing file without --format-version-bump.
add_executable(itantra-golden-generate test/golden/generate.cpp)
target_link_libraries(itantra-golden-generate PRIVATE itantra-core)
target_include_directories(itantra-golden-generate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_compile_options(itantra-golden-generate PRIVATE ${ITANTRA_HOST_WARNINGS})

# C-01/C-02 host preconditions, C-03. C-01/C-02 proper are [D] (Phase 11).
add_executable(c01_c04_test test/conformance/c01_c04.cpp)
target_link_libraries(c01_c04_test PRIVATE itantra-core)
target_include_directories(c01_c04_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_compile_options(c01_c04_test PRIVATE ${ITANTRA_HOST_WARNINGS})
add_test(NAME conformance.c01_c04
         COMMAND c01_c04_test --vectors "${CMAKE_CURRENT_SOURCE_DIR}/test/golden/vectors.bin")

# ---------------------------------------------------------------------------
# Phase 4 — encryption
# ---------------------------------------------------------------------------
itantra_host_test(unit.crypto crypto_test test/unit/crypto_test.cpp)
set_tests_properties(unit.crypto PROPERTIES SKIP_REGULAR_EXPRESSION "AEAD-BYPASSED")

# C-40, C-41. C-42 [P] and C-43 [D] are reported skipped by the binary.
add_executable(c40_c43_test test/conformance/c40_c43.cpp)
target_link_libraries(c40_c43_test PRIVATE itantra-core)
target_include_directories(c40_c43_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_compile_options(c40_c43_test PRIVATE ${ITANTRA_HOST_WARNINGS})
add_test(NAME conformance.c40_c43
         COMMAND c40_c43_test --vectors "${CMAKE_CURRENT_SOURCE_DIR}/test/golden/vectors.bin")
set_tests_properties(conformance.c40_c43 PROPERTIES SKIP_REGULAR_EXPRESSION "AEAD-BYPASSED")

# "ITANTRA_DISABLE_AEAD bypasses cleanly in debug".
add_executable(aead_bypass_test test/unit/aead_bypass_test.cpp)
target_link_libraries(aead_bypass_test PRIVATE itantra-core-aead-bypass)
target_include_directories(aead_bypass_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_compile_options(aead_bypass_test PRIVATE ${ITANTRA_HOST_WARNINGS})
add_test(NAME unit.aead-bypass
         COMMAND aead_bypass_test --vectors "${CMAKE_CURRENT_SOURCE_DIR}/test/golden/vectors.bin")
set_tests_properties(unit.aead-bypass PROPERTIES SKIP_REGULAR_EXPRESSION "AEAD-BYPASS-NOT-BUILT")

# C-43 host precondition: a translation unit with NDEBUG and ITANTRA_DISABLE_AEAD
# must fail to compile with the guard's own message. The probe is excluded
# from the normal build; this test builds it on purpose.
add_executable(c43_release_guard_probe EXCLUDE_FROM_ALL test/conformance/c43_release_guard_probe.cpp)
target_include_directories(c43_release_guard_probe PRIVATE "${ITANTRA_CORE_DIR}")
add_test(NAME C-43.precondition.release-guard
         COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target c43_release_guard_probe --config $<CONFIG>)
set_tests_properties(C-43.precondition.release-guard PROPERTIES
        PASS_REGULAR_EXPRESSION "ITANTRA_DISABLE_AEAD must not reach a release build"
        TIMEOUT 300)

# ---------------------------------------------------------------------------
# Phase 5 — Context Manager
# ---------------------------------------------------------------------------
# The second source file is a separate translation unit that takes commit()'s
# address: "commit() is ONE function — sender and receiver call the same symbol".
itantra_host_test(unit.context context_test test/unit/context_test.cpp)
target_sources(context_test PRIVATE test/unit/context_commit_other_tu.cpp)

itantra_host_test(C-32 c32_test test/conformance/c32.cpp)
