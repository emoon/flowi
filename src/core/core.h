#pragma once

#include "types.h"
#include <flowi/arena/arena.h> // @generated fl_init / fl_destroy (api_gen)
#include <flowi/core/platform.h>
#include <flowi/core/utils.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DLL export/import macros

#if PLATFORM_WINDOWS
#ifdef BASE_STATIC
#define DLL_EXPORT // Static library - no import/export
#elif defined(BASE_EXPORTS)
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __declspec(dllimport)
#endif
#else
#define DLL_EXPORT __attribute__((visibility("default")))
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILER_MSVC
#define align_of(type) __alignof(type)
#elif COMPILER_CLANG || COMPILER_GCC
#define align_of(type) __alignof__(type)
#else
#if __STDC_VERSION__ >= 201112L
#define align_of(type) _Alignof(type)
#else
#define align_of(type)   \
    ((size_t)&((struct { \
         char c;         \
         type t;         \
     }*)0)               \
         ->t)
#endif
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define thread_local _Thread_local
#else
#if COMPILER_MSVC
#define thread_local __declspec(thread)
#elif COMPILER_GCC || COMPILER_CLANG
#define thread_local __thread
#else
#define thread_local
#warning "THREAD_LOCAL not defined for this compiler; thread-local storage may not be supported."
#endif
#endif

#if COMPILER_MSVC
#define CACHE_ALIGNED __declspec(align(64))
#elif COMPILER_CLANG || COMPILER_GCC
#define CACHE_ALIGNED __attribute__((aligned(64)))
#else
#define CACHE_ALIGNED
#endif

