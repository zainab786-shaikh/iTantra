cmake_minimum_required(VERSION 3.22.1)

project("itantra-native" CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
        itantra-native
        SHARED
        itantra-native.cpp
)

find_library(
        log-lib
        log
)

target_link_libraries(
        itantra-native
        c++_shared
        ${log-lib}
)
