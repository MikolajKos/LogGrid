cmake_minimum_required(VERSION 3.25)
project(LogSearchDist CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Turn on all compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_subdirectory(NetCommon)
add_subdirectory(NetServer)
add_subdirectory(NetClient)

# ==========================================
# Doxygen Documentation Target
# ==========================================
find_package(Doxygen)
if(DOXYGEN_FOUND)
    # Options config
    set(DOXYGEN_PROJECT_NAME "LogGrid")
    set(DOXYGEN_EXTRACT_ALL "YES")
    set(DOXYGEN_GENERATE_HTML "YES")
    set(DOXYGEN_GENERATE_LATEX "NO") # Wyłączamy PDF, żeby przyspieszyć budowanie
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")

    # Creates new "doc" target
    doxygen_add_docs(doc
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/NetServer/src
        ${CMAKE_SOURCE_DIR}/NetClient/src
        COMMENT "Generating HTML documentation with Doxygen"
    )
else()
    message(WARNING "Doxygen not found - documentation will not be generated.")
endif()