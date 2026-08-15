find_package(Threads REQUIRED)

add_executable(NetServerApp 
    src/main.cpp 
    src/MasterServer.cpp
    src/HttpApi.cpp
    src/SearchController.cpp
)

target_link_libraries(NetServerApp
    PRIVATE
    NetCommon
    Threads::Threads
)

target_include_directories(NetServerApp
    PRIVATE
    ${CMAKE_SOURCE_DIR}/external
)