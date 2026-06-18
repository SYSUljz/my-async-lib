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

# --- 关键点：链接库 ---
add_executable(test_server main.cpp)
target_link_libraries(test_server PRIVATE ant_server ${URING_LIBRARIES})