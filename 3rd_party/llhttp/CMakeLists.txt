cmake_minimum_required(VERSION 3.16)
project(llhttp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)

add_library(llhttp STATIC
    src/llhttp.c
    src/api.c
    src/http.c
)

target_include_directories(llhttp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
