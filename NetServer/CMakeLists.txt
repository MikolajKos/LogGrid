add_executable(NetServerApp src/main.cpp src/MasterServer.cpp)

target_link_libraries(NetServerApp
    PRIVATE
    NetCommon
)