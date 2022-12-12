set(TARGET_NAME client)

set(SOURCES
    main.cpp
    echo_c.cpp
)
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SOURCES})

add_executable(${TARGET_NAME} ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE commlib)