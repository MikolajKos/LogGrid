file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(NetServerApp ${SERVER_SOURCES})

target_link_libraries(NetServerApp
    PRIVATE
    NetCommon
)