cmake_minimum_required(VERSION 3.16)
project(unlzx C)

set(UNLZX_SOURCES
    unlzx.c
)

add_library(unlzx STATIC ${UNLZX_SOURCES})

target_include_directories(unlzx
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Silence warnings in third-party code
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(unlzx PRIVATE -w)
endif()
