cmake_minimum_required(VERSION 3.22.1)

project("itantra-device-tests" CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# [D] conformance programs for Android devices — implementation plan Phase 11.
#
# A third entry point onto the single source list. Like the other two it owns
# no sources: it includes sources.cmake, so the programs a device runs are built
# from exactly the translation units in libitantra-native.so, with the NDK the
# app pins (app/build.gradle.kts ndkVersion). Standalone executables, pushed with
# adb and run from /data/local/tmp.
#
#   c01_device   C-01 / C-02 — configure Debug with -DITANTRA_DISABLE_AEAD=ON
#   c33_device   C-33        — configure Release (AEAD in the path)
#
# Configure per ABI, e.g. for arm64-v8a with the SDK's CMake and Ninja:
#
#   cmake -G Ninja -S native/test/device -B native/build/device/arm64-v8a-c01
#         -DCMAKE_TOOLCHAIN_FILE=<sdk>/ndk/27.0.12077973/build/cmake/android.toolchain.cmake
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=24 -DANDROID_STL=c++_shared
#         -DCMAKE_MAKE_PROGRAM=<sdk>/cmake/3.22.1/bin/ninja
#         -DCMAKE_BUILD_TYPE=Debug -DITANTRA_DISABLE_AEAD=ON
#
# ITANTRA_DISABLE_AEAD with a Release build type is a compile error from
# crypto/aead.h (contract C-43); this file does not second-guess that guard.

if(NOT ANDROID)
    message(FATAL_ERROR "native/test/device builds device programs: configure it with the NDK toolchain file. "
                        "Host runs of the same programs are in native/CMakeLists.txt.")
endif()

get_filename_component(ITANTRA_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../android-native/app/src/main/cpp" ABSOLUTE)
include("${ITANTRA_CPP_DIR}/sources.cmake")
get_filename_component(ITANTRA_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(ITANTRA_DISABLE_AEAD "Debug-only diagnostic: bypass AEAD (packet 6.10.2)" OFF)

add_library(itantra-device-core STATIC ${ITANTRA_CORE_SOURCES} ${ITANTRA_THIRD_PARTY_SOURCES})
target_include_directories(itantra-device-core PUBLIC "${ITANTRA_CORE_DIR}")
if(ITANTRA_DISABLE_AEAD)
    target_compile_definitions(itantra-device-core PUBLIC ITANTRA_DISABLE_AEAD)
endif()

# c++_shared named explicitly, exactly as android-native/app/src/main/cpp/CMakeLists.txt
# links libitantra-native.so: with this NDK and SDK path the toolchain adds no C++
# runtime to the link line by itself. Run with libc++_shared.so beside the program
# (LD_LIBRARY_PATH).
foreach(program c01_device c33_device)
    add_executable(${program} "${ITANTRA_TEST_DIR}/conformance/${program}.cpp")
    target_link_libraries(${program} PRIVATE itantra-device-core c++_shared)
    target_include_directories(${program} PRIVATE "${ITANTRA_TEST_DIR}")
endforeach()
