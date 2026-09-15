cmake_minimum_required(VERSION 3.22.1)

project("itantra-native" CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The AGP entry point. Its role is unchanged: AGP invokes this file, this file
# produces libitantra-native.so. What changed in Phase 0 is only that the
# source list now lives in sources.cmake, shared with the host build under
# native/, so the two cannot drift apart.
set(ITANTRA_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
include("${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake")

add_library(
        itantra-native
        SHARED
        ${ITANTRA_JNI_SOURCES}
        ${ITANTRA_CORE_SOURCES}
        ${ITANTRA_THIRD_PARTY_SOURCES}
)

target_include_directories(itantra-native PRIVATE "${ITANTRA_CPP_DIR}" "${ITANTRA_CORE_DIR}")

# ITANTRA_DISABLE_AEAD — packet §6.10.2, implementation plan Phase 11. A debug-only
# diagnostic for C-01 (isolate payload vs crypto), never a staging switch. Gradle
# passes it with -Pitantra.disableAead=true. It is applied as given: a build type
# that defines NDEBUG (AGP's release build) meets the #error in crypto/aead.h and
# fails to compile, which is contract C-43.
option(ITANTRA_DISABLE_AEAD "Debug-only diagnostic: bypass AEAD (packet 6.10.2)" OFF)
if(ITANTRA_DISABLE_AEAD)
    target_compile_definitions(itantra-native PRIVATE ITANTRA_DISABLE_AEAD)
    message(WARNING "ITANTRA_DISABLE_AEAD is ON: libitantra-native.so neither encrypts nor authenticates.")
endif()

find_library(
        log-lib
        log
)

target_link_libraries(
        itantra-native
        c++_shared
        ${log-lib}
)
