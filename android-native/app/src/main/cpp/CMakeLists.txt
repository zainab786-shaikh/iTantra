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

find_library(
        log-lib
        log
)

target_link_libraries(
        itantra-native
        c++_shared
        ${log-lib}
)
