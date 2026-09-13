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
        "${ITANTRA_CORE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/unit/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/unit/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance/*.cpp")
list(APPEND ITANTRA_C04_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test/itest.h")

# A core source outside native/src would silently escape the check.
foreach(core_source IN LISTS ITANTRA_CORE_SOURCES)
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
add_library(itantra-core STATIC ${ITANTRA_CORE_SOURCES})
target_include_directories(itantra-core PUBLIC "${ITANTRA_CORE_DIR}")
target_compile_options(itantra-core PRIVATE ${ITANTRA_HOST_WARNINGS})
add_dependencies(itantra-core itantra-c04)

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
