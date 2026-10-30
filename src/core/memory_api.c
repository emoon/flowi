#include "heap.h"
#include "memory.h"
#if USE_MIMALLOC
#include <mimalloc-stats.h>
#include <mimalloc.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Heap allocator implementation
//
// Wraps mimalloc, or the system allocator when mimalloc is disabled (sanitizers, Windows). All of
// these are thread-safe.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_heap_alloc(u64 size) {
#if USE_MIMALLOC
    return mi_malloc(size);
#else
    return malloc(size);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_heap_calloc(u64 count, u64 size) {
#if USE_MIMALLOC
    return mi_calloc(count, size);
#else
    return calloc(count, size);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_heap_realloc(void* ptr, u64 new_size) {
#if USE_MIMALLOC
    return mi_realloc(ptr, new_size);
#else
    return realloc(ptr, new_size);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_heap_free(void* ptr) {
#if USE_MIMALLOC
    mi_free(ptr);
#else
    free(ptr);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_heap_alloc_aligned(u64 alignment, u64 size) {
#if USE_MIMALLOC
    return mi_malloc_aligned(size, alignment);
#elif defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    // C11 aligned_alloc requires size to be an integral multiple of alignment (violating it is UB, and
    // the ASan interceptor flags it), but this allocator's contract only requires a power-of-two
    // alignment. Round size up to the next multiple; the power-of-two precondition makes the mask exact.
    u64 rounded = (size + alignment - 1) & ~(alignment - 1);
    return aligned_alloc(alignment, rounded);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_heap_free_aligned(void* ptr) {
#if USE_MIMALLOC
    mi_free(ptr);
#elif defined(_MSC_VER)
    // MSVC pairs _aligned_malloc with _aligned_free, not free.
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 fl_heap_usable_size(void* ptr) {
#if USE_MIMALLOC
    return mi_usable_size(ptr);
#else
    // The system allocator has no portable way to report this.
    (void)ptr;
    return 0;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mimalloc Statistics

MimallocStats mem_get_mimalloc_stats(void) {
    MimallocStats result = { 0 };

#if USE_MIMALLOC
    // Reclaims the calling thread's pages whose last block another thread freed, so they no longer count as live.
    mi_collect(false);
    mi_stats_merge();

    mi_stats_t stats;
    mi_stats_get(sizeof(stats), &stats);

    result.total_allocated = (u64)(stats.malloc_normal.total + stats.malloc_huge.total);
    u64 current = (u64)(stats.malloc_normal.current + stats.malloc_huge.current);
    result.total_freed = result.total_allocated - current;

    // Live blocks: the binned blocks plus one huge-bin page per huge block.
    u64 live_blocks = (u64)stats.page_bins[MI_BIN_HUGE].current;
    for (int bin = 0; bin < MI_BIN_HUGE; bin++) {
        live_blocks += (u64)stats.malloc_bins[bin].current;
    }

    result.alloc_count = (u64)(stats.malloc_normal_count.total + stats.malloc_huge_count.total);
    result.free_count = result.alloc_count > live_blocks ? result.alloc_count - live_blocks : 0;
#endif

    return result;
}
