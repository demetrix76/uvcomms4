set(TARGET_NAME server)

set(SOURCES
    main.cpp
    echo.cpp
)

source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SOURCES})

add_executable(${TARGET_NAME} ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE commlib)