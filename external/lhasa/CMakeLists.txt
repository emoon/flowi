cmake_minimum_required(VERSION 3.16)
project(lhasa C)

set(LHASA_SOURCES
    lib/crc16.c
    lib/ext_header.c
    lib/lha_decoder.c
    lib/lha_endian.c
    lib/lha_file_header.c
    lib/lha_input_stream.c
    lib/lha_basic_reader.c
    lib/lha_reader.c
    lib/macbinary.c
    lib/null_decoder.c
    lib/lh1_decoder.c
    lib/lh5_decoder.c
    lib/lh6_decoder.c
    lib/lh7_decoder.c
    lib/lhx_decoder.c
    lib/lk7_decoder.c
    lib/lz5_decoder.c
    lib/lzs_decoder.c
    lib/pm1_decoder.c
    lib/pm2_decoder.c
)

# Platform-specific sources
if(WIN32)
    list(APPEND LHASA_SOURCES lib/lha_arch_win32.c)
else()
    list(APPEND LHASA_SOURCES lib/lha_arch_unix.c)
endif()

add_library(lhasa STATIC ${LHASA_SOURCES})

target_include_directories(lhasa
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

# Silence warnings in third-party code
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lhasa PRIVATE -w)
endif()
