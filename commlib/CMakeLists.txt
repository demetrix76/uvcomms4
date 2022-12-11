set(TARGET_NAME commlib)

set(SOURCES
    commlib.h
    commlib.cpp
    pack.h
    collector.h
    delegate.h
    piper.h
    piper.cpp
    wrappers.h
    final_act.h
    request.h
)

if(CMAKE_SYSTEM_NAME MATCHES Linux)
    list(APPEND SOURCES commlib_lin.cpp)
elseif(CMAKE_SYSTEM_NAME MATCHES Darwin)
    list(APPEND SOURCES commlib_mac.cpp)
endif()

add_library(${TARGET_NAME} STATIC ${SOURCES})

target_link_libraries(${TARGET_NAME} PUBLIC UV::UV)
target_include_directories(${TARGET_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

if(UVCOMMS_THREAD_CHECKS)
    target_compile_definitions(${TARGET_NAME} PUBLIC UVCOMMS_THREAD_CHECKS)
endif()