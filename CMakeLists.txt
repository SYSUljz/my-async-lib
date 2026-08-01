cmake_minimum_required(VERSION 3.16)
project(ant_server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED liburing)

add_library(ant_server INTERFACE)
target_include_directories(ant_server INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)

# Executables for demo / server testing
add_executable(test_server test/main.cpp)
target_link_libraries(test_server PRIVATE ant_server ${URING_LIBRARIES})

add_executable(test_client test/client.cpp)

add_executable(test_timeout test/test_timeout.cpp)
target_link_libraries(test_timeout PRIVATE ant_server ${URING_LIBRARIES})

# --- GoogleTest Configuration ---
enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests test/test_read_awaiter_cancel.cpp)
target_link_libraries(unit_tests PRIVATE ant_server ${URING_LIBRARIES} gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)