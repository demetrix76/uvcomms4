set(TARGET_NAME server)

set(SOURCES
    main.cpp
    server.h
    server.cpp
)

add_executable(${TARGET_NAME} ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE commlib)