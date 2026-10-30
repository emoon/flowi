# fzy - Fast fuzzy string matching library
# https://github.com/jhawthorn/fzy

add_library(fzy STATIC
    match.c
)

target_include_directories(fzy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Create config.h from config.def.h if it doesn't exist
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.h")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/config.def.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/config.h"
        COPYONLY
    )
endif()

target_include_directories(fzy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compile with -fPIC for linking into shared libraries
set_target_properties(fzy PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Disable warnings for external library
if(MSVC)
    target_compile_options(fzy PRIVATE /W0)
else()
    target_compile_options(fzy PRIVATE -w)
endif()
