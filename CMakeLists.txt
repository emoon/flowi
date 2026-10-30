cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
    find_program(CLANG_FOUND clang)
    if(CLANG_FOUND)
        set(CMAKE_C_COMPILER clang)
    endif()
endif()

project(flowi C)

################################################################################
# Options

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

################################################################################
# Build options

option(FLOWI_GPU_BACKEND "Enabled to use the GPU backend" ON)
option(FLOWI_SDL_BACKEND "Enabled to use the SDL backend" ON)
option(FLOWI_VFS_DRIVER_LIBARCHIVE "Build the libarchive driver (ZIP, TAR, 7Z, RAR, ...)" OFF)
option(FLOWI_VFS_DRIVER_LHASA "Build the lhasa driver (Amiga LHA)" OFF)
option(FLOWI_VFS_DRIVER_UNLZX "Build the unlzx driver (Amiga LZX)" OFF)
include(CMakeDependentOption)
cmake_dependent_option(FLOWI_BUILD_EXAMPLES "Build the programs under examples/" ON "FLOWI_SDL_BACKEND" OFF)

################################################################################
# Compiler flags

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(TargetConfig)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i386|AMD64")
        set(SIMD_FLAGS -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(SIMD_FLAGS -march=armv8.2-a+fp16+simd)
    endif()

    add_compile_options(${SIMD_FLAGS})

    set(WARNING_FLAGS -Wall -Werror)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND WARNING_FLAGS -Wno-initializer-overrides)
    endif()
endif()

################################################################################
# Dependencies

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(SRC ${ROOT}/src)
set(GPU ${SRC}/render/gpu)

include_directories(${ROOT}/include ${SRC} ${ROOT} ${ROOT}/external)

find_package(ZLIB REQUIRED)

set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
if(APPLE OR WIN32 OR ENABLE_ASAN OR ENABLE_TSAN OR
   "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_FLAGS_RELEASE} ${CMAKE_C_FLAGS_RELWITHDEBINFO}" MATCHES "-fsanitize=[^ ]*(address|thread)")
    set(MI_OVERRIDE OFF CACHE BOOL "Override the C malloc family" FORCE)
else()
    set(MI_OVERRIDE ON CACHE BOOL "Override the C malloc family")
endif()

add_subdirectory(external/mimalloc)
target_compile_definitions(mimalloc-static PRIVATE MI_STAT=2)
add_subdirectory(external/xxhash)
add_subdirectory(external/fzy)
add_subdirectory(external/freetype2)

################################################################################
# Sources

file(GLOB SOURCES CONFIGURE_DEPENDS
    ${SRC}/app/*.c external/clay/clay.c ${SRC}/core/*.c ${SRC}/core/os/*.c ${SRC}/vfs/*.c ${SRC}/vfs/drivers/*.c
    ${SRC}/image_loader/*.c ${SRC}/platform/*.c ${SRC}/ui/*.c ${SRC}/ui/dock/*.c ${SRC}/render/software/*.c)

file(GLOB PLATFORM_SOURCES ${SRC}/platform/*.c)
set_source_files_properties(${PLATFORM_SOURCES} PROPERTIES COMPILE_DEFINITIONS BACKEND_USE_SDL=0)

file(GLOB VENDORED_SOURCES ${SRC}/image_loader/*.c ${ROOT}/external/clay/clay.c)
set_source_files_properties(${VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS -w)

file(GLOB OPTIMIZED_SOURCES ${SRC}/image_loader/*.c ${SRC}/render/software/*.c)
set_property(SOURCE ${OPTIMIZED_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -O3)

################################################################################
# SDL backend

if(FLOWI_SDL_BACKEND)
    find_package(SDL2 REQUIRED)
    file(GLOB SDL_SOURCES CONFIGURE_DEPENDS ${SRC}/platform/sdl/*.c)
    set_source_files_properties(${SDL_SOURCES} PROPERTIES COMPILE_DEFINITIONS BACKEND_USE_SDL=1)
    if(APPLE)
        set_source_files_properties(${SRC}/platform/sdl/backend_sdl_window.c PROPERTIES LANGUAGE OBJC)
    endif()
    list(APPEND SOURCES ${SDL_SOURCES})
endif()

################################################################################
# GPU backend

if (FLOWI_GPU_BACKEND)
    if(APPLE)
        enable_language(OBJC)
        file(GLOB GPU_BACKEND_SOURCES CONFIGURE_DEPENDS ${GPU}/metal/*.m)
        set_source_files_properties(${GPU}/shaders/gpu_shaders.c PROPERTIES COMPILE_DEFINITIONS FLOWI_SHADER_BACKEND_METAL=1)
        set(SHADER_COMPAT shader_compat_metal.h)
    else()
        find_package(OpenGL REQUIRED COMPONENTS EGL)
        file(GLOB GPU_BACKEND_SOURCES CONFIGURE_DEPENDS ${GPU}/gl/*.c)
        set(SHADER_COMPAT shader_compat_glsl.h)
    endif()

    file(GLOB SHADERS CONFIGURE_DEPENDS ${GPU}/shaders/*.glsl ${GPU}/shaders/${SHADER_COMPAT})
    foreach(shader IN LISTS SHADERS)
        get_filename_component(name ${shader} NAME_WE)
        set(embedded ${CMAKE_BINARY_DIR}/gpu_shaders/${name}.c)
        add_custom_command(OUTPUT ${embedded}
            COMMAND ${CMAKE_COMMAND} -DINPUT=${shader} -DOUTPUT=${embedded} -DSYMBOL=gpu_glsl_${name}
                    -P ${ROOT}/cmake/embed_glsl.cmake
            DEPENDS ${shader} ${ROOT}/cmake/embed_glsl.cmake)
        list(APPEND SOURCES ${embedded})
    endforeach()

    file(GLOB GPU_SHADER_SOURCES CONFIGURE_DEPENDS ${GPU}/shaders/*.c)
    list(APPEND SOURCES ${GPU_SHADER_SOURCES} ${GPU_BACKEND_SOURCES})
endif()

file(GLOB GPU_SOURCES CONFIGURE_DEPENDS ${GPU}/*.c)
list(APPEND SOURCES ${GPU_SOURCES})

################################################################################
# VFS drivers

if(FLOWI_VFS_DRIVER_LIBARCHIVE OR FLOWI_VFS_DRIVER_LHASA OR FLOWI_VFS_DRIVER_UNLZX)
    include(vfs_drivers)
endif()

foreach(driver LIBARCHIVE LHASA UNLZX)
    if(FLOWI_VFS_DRIVER_${driver})
        string(TOLOWER ${driver} name)
        file(GLOB driver_source CONFIGURE_DEPENDS ${SRC}/vfs/drivers/archive/${name}.c)
        list(APPEND SOURCES ${driver_source})
    endif()
endforeach()

################################################################################
# Library

add_library(flowi_objects OBJECT ${SOURCES})

target_include_directories(flowi_objects PUBLIC ${ROOT}/external/stb ${ROOT}/external/nanosvg)
target_include_directories(flowi_objects PUBLIC ${GPU})
target_compile_definitions(flowi_objects PRIVATE FL_API_IMPLEMENTATION=1 BACKEND_USE_DRM=0 BACKEND_RUNTIME_DETECTION=0)
target_compile_definitions(flowi_objects PUBLIC FLOWI_UI_INSPECTOR=1
    FLOWI_VFS_DRIVER_LIBARCHIVE=$<BOOL:${FLOWI_VFS_DRIVER_LIBARCHIVE}>
    FLOWI_VFS_DRIVER_LHASA=$<BOOL:${FLOWI_VFS_DRIVER_LHASA}>
    FLOWI_VFS_DRIVER_UNLZX=$<BOOL:${FLOWI_VFS_DRIVER_UNLZX}>)
target_link_libraries(flowi_objects PUBLIC freetype xxhash mimalloc-static fzy ZLIB::ZLIB $<$<NOT:$<BOOL:${WIN32}>>:m>)

if(WIN32)
    target_compile_definitions(flowi_objects PUBLIC BASE_STATIC=1 FL_API=)
endif()

if(FLOWI_VFS_DRIVER_LIBARCHIVE)
    target_link_libraries(flowi_objects PUBLIC archive_static)
    target_compile_definitions(flowi_objects PRIVATE LIBARCHIVE_STATIC)
endif()
if(FLOWI_VFS_DRIVER_LHASA)
    target_link_libraries(flowi_objects PUBLIC lhasa)
endif()
if(FLOWI_VFS_DRIVER_UNLZX)
    target_link_libraries(flowi_objects PUBLIC unlzx)
endif()

################################################################################

if(APPLE)
    target_link_libraries(flowi_objects PUBLIC "-framework CoreServices" "-framework CoreFoundation")
endif()

################################################################################

if(FLOWI_SDL_BACKEND)
    target_compile_definitions(flowi_objects PRIVATE FL_APP_HAVE_SDL=1)
    target_link_libraries(flowi_objects PUBLIC SDL2::SDL2)
    if(APPLE)
        target_link_libraries(flowi_objects PUBLIC "-framework QuartzCore")
    else()
        target_compile_definitions(flowi_objects PRIVATE FL_WINDOW_HAVE_SOFTWARE=1)
    endif()
endif()

################################################################################

if(FLOWI_GPU_BACKEND)
    target_compile_definitions(flowi_objects PUBLIC FL_APP_HAVE_GPU=1)
    if(APPLE)
        target_link_libraries(flowi_objects PUBLIC "-framework Metal" "-framework Foundation" "-framework IOSurface")
        if(FLOWI_SDL_BACKEND)
            target_compile_definitions(flowi_objects PRIVATE FL_WINDOW_HAVE_METAL=1)
        endif()
    else()
        target_link_libraries(flowi_objects PUBLIC OpenGL::EGL OpenGL::GL)
        if(FLOWI_SDL_BACKEND)
            target_compile_definitions(flowi_objects PRIVATE FL_WINDOW_HAVE_GPU=1)
        endif()
    endif()
endif()

apply_target_config(flowi_objects)

add_library(flowi SHARED)
target_link_libraries(flowi PRIVATE flowi_objects)
apply_target_config(flowi)

add_library(flowi_static STATIC)
target_link_libraries(flowi_static PRIVATE flowi_objects)
apply_target_config(flowi_static)

################################################################################
# Exports

if (WIN32)
    target_sources(flowi PRIVATE src/exports/flowi.def)
elseif (UNIX AND NOT APPLE)
    target_link_options(flowi PRIVATE -Wl,--version-script=${ROOT}/src/exports/flowi.version)
    set_target_properties(flowi PROPERTIES LINK_DEPENDS ${ROOT}/src/exports/flowi.version)
endif()

################################################################################
# Tools

add_executable(loc_check ${SRC}/tools/loc_check_main.c ${SRC}/tools/loc_check.c)
target_link_libraries(loc_check PRIVATE flowi_objects)
apply_target_config(loc_check)

################################################################################
# Tests

enable_language(CXX)
enable_testing()

add_library(external_abi_probe SHARED tests/probes/external_c_abi.c tests/probes/external_cpp_abi.cpp)
target_link_libraries(external_abi_probe PRIVATE flowi)

add_library(shared_library_smoke_probe SHARED tests/probes/shared_library_smoke_probe.c)
target_link_libraries(shared_library_smoke_probe PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(shared_library_smoke_probe flowi)

file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*.c tests/probes/*_api_check.c)
add_executable(unit_tests ${TEST_SRC} ${SRC}/tools/loc_check.c)
target_link_libraries(unit_tests PRIVATE flowi_objects external_abi_probe shared_library_smoke_probe)
target_compile_definitions(unit_tests PRIVATE FL_API_IMPLEMENTATION=1
    FLOWI_TEST_DATA_DIR="${ROOT}/examples/data" FLOWI_TEST_FIXTURE_DIR="${ROOT}/tests/fixtures"
    FLOWI_SHARED_LIBRARY_PATH="$<TARGET_FILE:flowi>")
if(FLOWI_SDL_BACKEND)
    target_compile_definitions(unit_tests PRIVATE BACKEND_USE_SDL=1)
endif()
if(UNIX AND NOT APPLE)
    file(WRITE ${CMAKE_BINARY_DIR}/unit_tests.version "{ local: *; };\n")
    target_link_options(unit_tests PRIVATE -Wl,--version-script=${CMAKE_BINARY_DIR}/unit_tests.version)
endif()
add_test(NAME unit_tests COMMAND unit_tests)

if(FLOWI_GPU_BACKEND)
    file(GLOB GPU_TEST_SRC CONFIGURE_DEPENDS tests/gpu/*.c)
    if(APPLE)
        file(GLOB GPU_TEST_OBJC CONFIGURE_DEPENDS tests/gpu/*.m)
        list(APPEND GPU_TEST_SRC ${GPU_TEST_OBJC})
    endif()
    add_executable(gpu_smoke_test ${GPU_TEST_SRC})
    target_include_directories(gpu_smoke_test PRIVATE tests)
    target_link_libraries(gpu_smoke_test PRIVATE flowi_objects)
    add_test(NAME gpu_smoke_test COMMAND gpu_smoke_test)
endif()

################################################################################
# Examples

if(FLOWI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
