set(TARGET_NAME client)

set(SOURCES
    main.cpp
    client.h
    client.cpp
)

add_executable(${TARGET_NAME} ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE commlib)