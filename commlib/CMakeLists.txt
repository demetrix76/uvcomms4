set(TARGET_NAME commlib)

set(SOURCES
    commlib.h
    commlib.cpp
    uvx.h
    Streamer.h
)

if(CMAKE_SYSTEM_NAME MATCHES Linux)
    list(APPEND SOURCES commlib_lin.cpp)
endif()

add_library(${TARGET_NAME} STATIC ${SOURCES})

target_link_libraries(${TARGET_NAME} PUBLIC UV::UV)
target_include_directories(${TARGET_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)