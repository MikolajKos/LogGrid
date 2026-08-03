add_executable(LogGridTests
    test_thread_pool.cpp
    test_file_processor.cpp
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