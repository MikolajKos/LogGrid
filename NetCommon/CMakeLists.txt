add_library(NetCommon INTERFACE)

target_include_directories(NetCommon INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(NetCommon INTERFACE ASIO_STANDALONE)

target_link_libraries(NetCommon INTERFACE Threads::Threads)