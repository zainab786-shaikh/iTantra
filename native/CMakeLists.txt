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

include("${ITANTRA_CPP_DIR}/sources.cmake")

# ITANTRA_JNI_SOURCES is intentionally not used here. jni.h and android/log.h
# are not available on the host, and the JNI layer is a thin adapter with no
# logic of its own to test (handoff: "JNI BOUNDARY" — coarse, one call per
# clause).

if(ITANTRA_CORE_SOURCES)
    add_library(itantra-core STATIC ${ITANTRA_CORE_SOURCES})
    target_include_directories(itantra-core PUBLIC "${ITANTRA_CPP_DIR}")
else()
    # Phase 0: the portable core is empty by design — Phase 0 is
    # reconciliation only, "No new logic". Configuring cleanly now means
    # Phase 1 adds files to sources.cmake and the host build starts working
    # with no further build-system work.
    message(STATUS
            "itantra: portable core is empty (expected in Phase 0). "
            "Phase 1 populates ITANTRA_CORE_SOURCES in sources.cmake.")
endif()

# Phase 1 adds here:
#   - the test framework
#   - enable_testing() and the [H] unit / conformance / bench targets
#   - the C-04 no-float static check, as a BUILD STEP that fails the build
#     (implementation plan, Phase 1 Notes)
