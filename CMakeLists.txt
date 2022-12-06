cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

project(sockuv4)

# going the Linux way for now
find_package(PkgConfig REQUIRED)
pkg_check_modules(UV REQUIRED IMPORTED_TARGET libuv>=1.44)
add_library(UV::UV ALIAS PkgConfig::UV)

add_subdirectory(server)
add_subdirectory(commlib)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG        release-1.12.1
)

FetchContent_MakeAvailable(googletest)

add_subdirectory(test)