set(TARGET_NAME test)

set(SOURCES
    test1.cpp
    messagemock.h
)

enable_testing()

add_executable(${TARGET_NAME} ${SOURCES})

source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE commlib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test)