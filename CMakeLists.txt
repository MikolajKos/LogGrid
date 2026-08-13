cmake_minimum_required(VERSION 3.25)
project(LogGrid CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Turn on all compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
endif()

# ==========================================
# GoogleTest Setup
# ==========================================

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Prevents compiler flags from being overwritten
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ==========================================

add_subdirectory(NetCommon)
add_subdirectory(NetServer)
add_subdirectory(NetClient)
add_subdirectory(tests)

# ==========================================
# Code Coverage
# ==========================================
option(ENABLE_COVERAGE "Enable gcov/lcov code coverage" OFF)

if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Coverage requires GCC or Clang")
    endif()

    find_program(LCOV    lcov    REQUIRED)
    find_program(GENHTML genhtml REQUIRED)

    target_compile_options(LogGridTests PRIVATE --coverage)
    target_link_options(LogGridTests    PRIVATE --coverage)

    set(COVERAGE_DIR "${CMAKE_BINARY_DIR}/coverage")

    add_custom_target(coverage
        COMMENT "Generating code coverage report..."
        # 1. Zero old counters
        COMMAND ${LCOV} --zerocounters --directory ${CMAKE_BINARY_DIR}
        # 2. Run tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        # 3. Capture
        COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_DIR}
        COMMAND ${LCOV} --capture
                --directory ${CMAKE_BINARY_DIR}
                --output-file ${COVERAGE_DIR}/coverage.info
                --ignore-errors mismatch
        # 4. Filter out system headers, GoogleTest, external deps, test files themselves
        COMMAND ${LCOV} --remove ${COVERAGE_DIR}/coverage.info
                "/usr/*"
                "${CMAKE_BINARY_DIR}/_deps/*"
                "${CMAKE_SOURCE_DIR}/external/*"
                "${CMAKE_SOURCE_DIR}/tests/*"
                --output-file ${COVERAGE_DIR}/coverage_filtered.info
                --ignore-errors unused
        # 5. HTML report (also prints summary)
        COMMAND ${GENHTML} ${COVERAGE_DIR}/coverage_filtered.info
                --output-directory ${COVERAGE_DIR}/html
                --title "LogGrid Coverage"
                --legend
        DEPENDS LogGridTests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

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
        ${CMAKE_SOURCE_DIR}/NetClient/include
        ${CMAKE_SOURCE_DIR}/NetServer/src
        ${CMAKE_SOURCE_DIR}/NetClient/src
        COMMENT "Generating HTML documentation with Doxygen"
    )
else()
    message(WARNING "Doxygen not found - documentation will not be generated.")
endif()

# ==========================================