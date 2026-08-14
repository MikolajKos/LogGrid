# Unit tests executable
add_executable(LogGridTests
    test_thread_pool.cpp
    test_file_processor.cpp
    test_worker_client.cpp

    ${CMAKE_SOURCE_DIR}/NetClient/src/WorkerClient.cpp
)

target_link_libraries(LogGridTests
    PRIVATE
    gtest
    gtest_main
    NetCommon
)

target_include_directories(LogGridTests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/NetClient/src
)

include(GoogleTest)
gtest_discover_tests(LogGridTests)

# Integration tests executable
add_executable(LogGridIntegrationTests
    test_worker_integration.cpp
    ${CMAKE_SOURCE_DIR}/NetClient/src/WorkerClient.cpp
)

target_link_libraries(LogGridIntegrationTests
    PRIVATE
    gtest
    gtest_main
    NetCommon
)

target_include_directories(LogGridIntegrationTests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/NetClient/src
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(LogGridIntegrationTests)