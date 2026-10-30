# XXHash - Extremely Fast Hash algorithm
# BSD 2-Clause License
# Copyright (C) 2012-2023 Yann Collet

cmake_minimum_required(VERSION 3.16)

project(xxhash VERSION 0.8.3 LANGUAGES C)

# XXHash is used as a header-only library with XXH_INLINE_ALL
# This provides the best performance by inlining all hash functions
# No library target is needed - just provide the include directory

# Create an interface library for header-only usage
add_library(xxhash INTERFACE)

# Set public include directories
target_include_directories(xxhash INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)