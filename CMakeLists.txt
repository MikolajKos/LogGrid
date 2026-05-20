cmake_minimum_required(VERSION 3.25)
project(NetworkingSolution)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_subdirectory(NetCommon)
add_subdirectory(NetServer)
add_subdirectory(NetClient)
