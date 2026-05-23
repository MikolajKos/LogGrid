add_library(NetCommon INTERFACE)

target_include_directories(NetCommon INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/asio/include
)

target_compile_definitions(NetCommon INTERFACE ASIO_STANDALONE)

if (WIN32)
    # Defines minimum Windows API version
    target_compile_definitions(NetCommon INTERFACE _WIN32_WINNT=0x0601)

    # Links system libraries for windows
    target_link_libraries(NetCommon INTERFACE ws2_32 mswsock)
elseif(UNIX)
    find_package(Threads REQUIRED)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    target_link_libraries(NetCommon INTERFACE Threads::Threads)
endif()
