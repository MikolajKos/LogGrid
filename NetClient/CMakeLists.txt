add_executable(NetClientApp src/main.cpp src/WorkerClient.cpp)

target_link_libraries(NetClientApp
    PRIVATE
    NetCommon
)
