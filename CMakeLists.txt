cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

project(sockuv4)

option(UVCOMMS_THREAD_CHECKS "Verify that certain functions are called on the thread they are supposed to" NO)

if(CMAKE_SYSTEM_NAME MATCHES Linux)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UV REQUIRED IMPORTED_TARGET libuv>=1.44)
    add_library(UV::UV ALIAS PkgConfig::UV)
else()
    find_package(uvw CONFIG REQUIRED)
    add_library(UV::UV ALIAS uvw::uvw)
endif()

add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(commlib)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG        release-1.12.1
)

FetchContent_MakeAvailable(googletest)

add_subdirectory(test)