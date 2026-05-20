add_executable(NetServerApp src/main.cpp)

target_link_libraries(NetServerApp
    PRIVATE
    NetCommon
)
