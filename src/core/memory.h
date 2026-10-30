#pragma once

#include "core.h"
#include "types.h"

// Request C11 Annex K secure functions (memset_s, etc.) before including string.h
#if COMPILER_MSVC
#define __STDC_WANT_LIB_EXT1__ 1
#endif

#include <string.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer) || defined(__SANITIZE_ADDRESS__) \
    || __has_feature(address_sanitizer)
#define SANITIZERS_ENABLED 1
#else
#define SANITIZERS_ENABLED 0
#endif

// A build selects the allocator by defining USE_MIMALLOC; sanitizers and Windows force it off.
#if SANITIZERS_ENABLED || PLATFORM_WINDOWS
#undef USE_MIMALLOC
#define USE_MIMALLOC 0
#elif !defined(USE_MIMALLOC)
#define USE_MIMALLOC 1
#endif

#if USE_MIMALLOC
#include <mimalloc.h>
#endif

#ifdef __STDC_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UNUSED_FUNCTION static void memory_zero(void* ptr, u64 size) {
#if COMPILER_MSVC
    // SecureZeroMemory would require windows.h
    volatile unsigned char* p = (volatile unsigned char*)ptr;
    while (size--)
        *p++ = 0;
#elif defined(__STDC_LIB_EXT1__)
    memset_s(ptr, size, 0, size);
#elif COMPILER_GCC || COMPILER_CLANG
    __builtin_memset(ptr, 0, size);
#else
    memset(ptr, 0, size);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void memory_copy(void* restrict dest, u64 dest_size, const void* restrict src, u64 src_size) {
#if COMPILER_MSVC
    memcpy_s(dest, dest_size, src, src_size);
#elif defined(__STDC_LIB_EXT1__)
    memcpy_s(dest, dest_size, src, src_size);
#elif COMPILER_GCC || COMPILER_CLANG
    (void)dest_size; // Suppress unused parameter warning
    __builtin_memcpy(dest, src, src_size);
#else
    (void)dest_size; // Suppress unused parameter warning
    memcpy(dest, src, src_size);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void memory_set(void* ptr, u64 ptr_size, int value, u64 count) {
#if defined(__STDC_LIB_EXT1__) && !COMPILER_MSVC
    // C11 Annex K is not supported on MSVC
    memset_s(ptr, ptr_size, value, count);
#else
    (void)ptr_size; // Suppress unused parameter warning
    memset(ptr, value, count);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int memory_compare(const void* ptr1, const void* ptr2, u64 size) {
#if COMPILER_GCC || COMPILER_CLANG
    return __builtin_memcmp(ptr1, ptr2, size);
#else
    return memcmp(ptr1, ptr2, size);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if !USE_MIMALLOC
#include <stdlib.h>
#define mi_malloc malloc
#define mi_free free
#define mi_realloc realloc
#define mi_zalloc(size) calloc(1, size)
#define mi_alloc_zero(type) ((type*)calloc(1, sizeof(type)))
#define mi_alloc(type) ((type*)malloc(sizeof(type)))
#else
#define mi_alloc_zero(type) ((type*)mi_zalloc(sizeof(type)))
#define mi_alloc(type) ((type*)mi_malloc(sizeof(type)))
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// ASAN memory poisoning support
#if defined(ENABLE_ASAN) || defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#ifdef __cplusplus
extern "C" {
#endif
void __asan_poison_memory_region(void const volatile* addr, size_t size);
void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
#ifdef __cplusplus
}
#endif
#define asan_poison_memory_region(addr, size) __asan_poison_memory_region((addr), (size))
#define asan_unpoison_memory_region(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#define asan_poison_memory_region(addr, size) ((void)(addr), (void)(size))
#define asan_unpoison_memory_region(addr, size) ((void)(addr), (void)(size))
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mimalloc Statistics

typedef struct {
    u64 total_allocated;
    u64 total_freed;
    u64 alloc_count;
    u64 free_count;
} MimallocStats;

MimallocStats mem_get_mimalloc_stats(void);
