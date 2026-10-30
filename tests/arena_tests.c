#include "utest/utest.h"
#include <core/arena.h>
#include <core/arena_typed.h>
#include <core/memory.h>
#include <stdint.h>
#include <string.h>

static bool arena_ptr_is_aligned(void* ptr, u64 alignment) {
    return ((uintptr_t)ptr & (alignment - 1)) == 0;
}

static void fill_memory_pattern(void* ptr, u64 size, u8 pattern) {
    memory_set(ptr, size, pattern, size);
}

static bool check_memory_pattern(void* ptr, u64 size, u8 pattern) {
    u8* bytes = (u8*)ptr;
    for (u64 i = 0; i < size; i++) {
        if (bytes[i] != pattern) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// Basic FlArena Tests
// =============================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, new_and_destroy) {
    FlArena* arena = arena_new(.reserved_size = GB(2));
    ASSERT_NE(arena, NULL);

    void* ptr = fl_arena_alloc_raw(arena, 64, 8);
    ASSERT_NE(ptr, NULL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, alloc_raw_basic) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* ptr = fl_arena_alloc_raw(arena, 1024, 16);
    ASSERT_NE(ptr, NULL);
    ASSERT_TRUE(arena_ptr_is_aligned(ptr, 16));

    fill_memory_pattern(ptr, 1024, 0xAB);
    ASSERT_TRUE(check_memory_pattern(ptr, 1024, 0xAB));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, alloc_raw_alignment) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* ptr1 = fl_arena_alloc_raw(arena, 64, 1);
    ASSERT_TRUE(arena_ptr_is_aligned(ptr1, 1));

    void* ptr4 = fl_arena_alloc_raw(arena, 64, 4);
    ASSERT_TRUE(arena_ptr_is_aligned(ptr4, 4));

    void* ptr16 = fl_arena_alloc_raw(arena, 64, 16);
    ASSERT_TRUE(arena_ptr_is_aligned(ptr16, 16));

    void* ptr64 = fl_arena_alloc_raw(arena, 64, 64);
    ASSERT_TRUE(arena_ptr_is_aligned(ptr64, 64));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, alloc_array) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    u32* array = arena_alloc_array(arena, u32, 100);
    ASSERT_NE(array, NULL);
    ASSERT_TRUE(arena_ptr_is_aligned(array, _Alignof(u32)));

    for (u64 i = 0; i < 100; i++) {
        array[i] = (u32)(i * 2);
    }

    for (u64 i = 0; i < 100; i++) {
        ASSERT_EQ(array[i], (u32)(i * 2));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A wrapped count * sizeof(type) would reserve a fraction of the bytes the caller goes on to write
// count elements into. The saturated size reaches the allocator's fatal size-overflow path instead,
// which exits the process - so this case checks the size the macros compute rather than allocating it.
UTEST(arena, array_size_saturates_instead_of_wrapping) {
    ASSERT_EQ(arena_array_bytes(0, sizeof(u32)), (u64)0);
    ASSERT_EQ(arena_array_bytes(100, sizeof(u32)), (u64)100 * sizeof(u32));
    ASSERT_EQ(arena_array_bytes(UINT64_MAX, 1), (u64)UINT64_MAX);

    const u64 largest_representable = UINT64_MAX / sizeof(u64);
    ASSERT_EQ(arena_array_bytes(largest_representable, sizeof(u64)), largest_representable * sizeof(u64));
    ASSERT_EQ(arena_array_bytes(largest_representable + 1, sizeof(u64)), (u64)UINT64_MAX);
    ASSERT_EQ(arena_array_bytes(UINT64_MAX, sizeof(u64)), (u64)UINT64_MAX);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The size goes through a function call, so a count with a side effect runs once - the ternary the
// saturation could have been written as would run it twice.
UTEST(arena, array_macros_evaluate_count_once) {
    FlArena* arena = arena_new(.reserved_size = MB(1));

    u64 evaluations = 0;
    u64* array = arena_alloc_array(arena, u64, (evaluations++, 4));
    ASSERT_NE(array, NULL);
    ASSERT_EQ(evaluations, (u64)1);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, typed_allocation_macros) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    u64* single = arena_alloc(arena, u64);
    ASSERT_NE(single, NULL);
    ASSERT_TRUE(arena_ptr_is_aligned(single, _Alignof(u64)));

    *single = 0xDEADBEEFCAFEBABE;
    ASSERT_EQ(*single, 0xDEADBEEFCAFEBABE);

    f32* floats = arena_alloc_array(arena, f32, 50);
    ASSERT_NE(floats, NULL);
    ASSERT_TRUE(arena_ptr_is_aligned(floats, _Alignof(f32)));

    for (u64 i = 0; i < 50; i++) {
        floats[i] = (f32)i * 1.5f;
    }

    for (u64 i = 0; i < 50; i++) {
        ASSERT_NEAR(floats[i], (f32)i * 1.5f, 1e-6f);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, typed_rewind_stops_at_rewind_floor) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    arena_typed(u64) values;
    arena_typed_init(&values, arena);

    u64* header = arena_typed_alloc(&values);
    *header = 0xDEADBEEFCAFEBABE;

    // Pin the floor above the typed arena's start, the way arena_mt_new_ protects its wrapper.
    arena_pin_rewind_floor(arena);
    void* floor = fl_arena_current_pos(arena);

    u64* scratch = arena_typed_alloc(&values);
    *scratch = 1;

    arena_typed_rewind(&values);

    ASSERT_TRUE(fl_arena_current_pos(arena) == floor);
    ASSERT_EQ(*header, 0xDEADBEEFCAFEBABE);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, typed_rewind_releases_elements) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    arena_typed(u32) values;
    arena_typed_init(&values, arena);

    for (u32 i = 0; i < 16; i++) {
        *arena_typed_alloc(&values) = i;
    }
    ASSERT_EQ(arena_typed_count(&values), 16u);

    arena_typed_rewind(&values);
    ASSERT_EQ(arena_typed_count(&values), 0u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, multiple_allocations) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* ptrs[10];

    for (int i = 0; i < 10; i++) {
        ptrs[i] = fl_arena_alloc_raw(arena, 256, 8);
        ASSERT_NE(ptrs[i], NULL);

        fill_memory_pattern(ptrs[i], 256, (u8)(i + 1));
    }

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(check_memory_pattern(ptrs[i], 256, (u8)(i + 1)));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, rewind) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* ptr1 = fl_arena_alloc_raw(arena, 1024, 8);
    ASSERT_NE(ptr1, NULL);
    fill_memory_pattern(ptr1, 1024, 0xCC);

    void* ptr2 = fl_arena_alloc_raw(arena, 512, 8);
    ASSERT_NE(ptr2, NULL);
    fill_memory_pattern(ptr2, 512, 0xDD);

    fl_arena_rewind(arena);

    void* ptr3 = fl_arena_alloc_raw(arena, 2048, 8);
    ASSERT_NE(ptr3, NULL);

    // In debug mode ptr3 may differ from ptr1 due to memory protection swapping
    fill_memory_pattern(ptr3, 2048, 0xEE);
    ASSERT_TRUE(check_memory_pattern(ptr3, 2048, 0xEE));

    fl_arena_destroy(arena);
}

// =============================================================================
// Large Allocation Tests
// =============================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, large_allocation) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* large_ptr = fl_arena_alloc_raw(arena, MB(8), 64);
    ASSERT_NE(large_ptr, NULL);
    ASSERT_TRUE(arena_ptr_is_aligned(large_ptr, 64));

    u8* bytes = (u8*)large_ptr;
    bytes[0] = 0xAA;
    bytes[MB(4)] = 0xBB;
    bytes[MB(8) - 1] = 0xCC;

    ASSERT_EQ(bytes[0], 0xAA);
    ASSERT_EQ(bytes[MB(4)], 0xBB);
    ASSERT_EQ(bytes[MB(8) - 1], 0xCC);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, many_small_allocations) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    const int num_allocs = 1000;
    // MSVC doesn't support VLAs - use heap allocation
    void** ptrs = (void**)malloc(num_allocs * sizeof(void*));
    ASSERT_NE(ptrs, NULL);

    for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = fl_arena_alloc_raw(arena, 64, 8);
        ASSERT_NE(ptrs[i], NULL);

        *(u32*)ptrs[i] = (u32)i;
    }

    for (int i = 0; i < num_allocs; i++) {
        ASSERT_EQ(*(u32*)ptrs[i], (u32)i);
    }

    free(ptrs);
    fl_arena_destroy(arena);
}

// =============================================================================
// Stress Tests
// =============================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, stress_alloc_rewind_cycle) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    for (int cycle = 0; cycle < 100; cycle++) {
        void* ptr1 = fl_arena_alloc_raw(arena, 1024, 8);
        ASSERT_NE(ptr1, NULL);

        void* ptr2 = fl_arena_alloc_raw(arena, 2048, 16);
        ASSERT_NE(ptr2, NULL);

        void* ptr3 = fl_arena_alloc_raw(arena, 512, 32);
        ASSERT_NE(ptr3, NULL);

        fill_memory_pattern(ptr1, 1024, 0x11);
        fill_memory_pattern(ptr2, 2048, 0x22);
        fill_memory_pattern(ptr3, 512, 0x33);

        ASSERT_TRUE(check_memory_pattern(ptr1, 1024, 0x11));
        ASSERT_TRUE(check_memory_pattern(ptr2, 2048, 0x22));
        ASSERT_TRUE(check_memory_pattern(ptr3, 512, 0x33));

        fl_arena_rewind(arena);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, mixed_size_allocations) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    u8* small_value = (u8*)fl_arena_alloc_raw(arena, 1, 1);
    *small_value = 0xAA;

    u16* medium = (u16*)fl_arena_alloc_raw(arena, sizeof(u16), _Alignof(u16));
    *medium = 0xBBCC;

    u64* large = (u64*)fl_arena_alloc_raw(arena, sizeof(u64), _Alignof(u64));
    *large = 0xDDEEFF0011223344;

    void* misaligned = fl_arena_alloc_raw(arena, 13, 1);
    fill_memory_pattern(misaligned, 13, 0x55);

    ASSERT_EQ(*small_value, 0xAA);
    ASSERT_EQ(*medium, 0xBBCC);
    ASSERT_EQ(*large, 0xDDEEFF0011223344);
    ASSERT_TRUE(check_memory_pattern(misaligned, 13, 0x55));

    fl_arena_destroy(arena);
}

// =============================================================================
// Edge Cases
// =============================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, compacting) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    u64 initial_pos = arena->pos;

    const u64 max_size = 100;
    int* array = arena_alloc_array(arena, int, max_size);
    ASSERT_NE(array, NULL);

    u64 pos_after_alloc = arena->pos;
    ASSERT_EQ(pos_after_alloc - initial_pos, max_size * sizeof(int));

    const u64 actual_count = 30;
    for (u64 i = 0; i < actual_count; i++) {
        array[i] = (int)(i * 10);
    }

    const u64 unused_count = max_size - actual_count;
    arena_compact(arena, int, unused_count);

    u64 pos_after_compact = arena->pos;
    ASSERT_EQ(pos_after_compact - initial_pos, actual_count * sizeof(int));

    for (u64 i = 0; i < actual_count; i++) {
        ASSERT_EQ(array[i], (int)(i * 10));
    }

    double* next_alloc = arena_alloc(arena, double);
    ASSERT_NE(next_alloc, NULL);
    *next_alloc = 3.14159;
    ASSERT_EQ(*next_alloc, 3.14159);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, zero_size_allocation) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* ptr = fl_arena_alloc_raw(arena, 0, 8);
    ASSERT_NE(ptr, NULL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, alignment_edge_cases) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* ptr1 = fl_arena_alloc_raw(arena, 1, 1);
    ASSERT_TRUE(arena_ptr_is_aligned(ptr1, 1));

    void* ptr2 = fl_arena_alloc_raw(arena, 64, 256);
    ASSERT_TRUE(arena_ptr_is_aligned(ptr2, 256));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, page_boundary_allocations) {
    FlArena* arena = arena_new(.reserved_size = GB(2));

    void* ptr1 = fl_arena_alloc_raw(arena, KB(4), 8);
    ASSERT_NE(ptr1, NULL);

    void* ptr2 = fl_arena_alloc_raw(arena, KB(4), 8);
    ASSERT_NE(ptr2, NULL);

    fill_memory_pattern(ptr1, KB(4), 0xAA);
    fill_memory_pattern(ptr2, KB(4), 0xBB);

    ASSERT_TRUE(check_memory_pattern(ptr1, KB(4), 0xAA));
    ASSERT_TRUE(check_memory_pattern(ptr2, KB(4), 0xBB));

    fl_arena_destroy(arena);
}

// =============================================================================
// Memory Size Macro Tests
// =============================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, memory_size_macros) {
    FlArena* arena1 = arena_new(.reserved_size = GB(2));
    fl_arena_destroy(arena1);

    FlArena* arena2 = arena_new(.reserved_size = GB(2));

    void* ptr = fl_arena_alloc_raw(arena2, KB(512), 8);
    ASSERT_NE(ptr, NULL);

    fl_arena_destroy(arena2);
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

#include <core/os/os.h>
typedef struct ThreadTestData {
    FlArenaMt* arena_mt;
    int thread_id;
    int num_allocations;
    _Atomic u32 errors;
} ThreadTestData;

static void* thread_alloc_test(void* arg) {
    ThreadTestData* data = (ThreadTestData*)arg;
    FlArenaMt* arena_mt = data->arena_mt;

    for (int i = 0; i < data->num_allocations; i++) {
        size_t size = 256 + (i % 512);
        u8 pattern = (u8)((data->thread_id << 4) | (i & 0xF));

        void* ptr = arena_mt_alloc_raw(arena_mt, size, 8);
        if (!ptr) {
            atomic_fetch_add(&data->errors, 1);
            continue;
        }

        fill_memory_pattern(ptr, size, pattern);

        if (!check_memory_pattern(ptr, size, pattern)) {
            atomic_fetch_add(&data->errors, 1);
        }
    }

    return NULL;
}

static void* thread_large_alloc_test(void* arg) {
    ThreadTestData* data = (ThreadTestData*)arg;
    FlArenaMt* arena_mt = data->arena_mt;
    const int large_allocation_size = MB(2);

    for (int i = 0; i < data->num_allocations; i++) {
        void* ptr = arena_mt_alloc_raw(arena_mt, large_allocation_size, 64);
        if (!ptr) {
            atomic_fetch_add(&data->errors, 1);
            continue;
        }

        u8* bytes = (u8*)ptr;
        u8 pattern = (u8)(data->thread_id + i);
        bytes[0] = pattern;
        bytes[large_allocation_size / 2] = pattern;
        bytes[large_allocation_size - 1] = pattern;

        if (bytes[0] != pattern || bytes[large_allocation_size / 2] != pattern
            || bytes[large_allocation_size - 1] != pattern) {
            atomic_fetch_add(&data->errors, 1);
        }
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, thread_safe_allocation) {
    FlArenaMt* arena_mt = arena_mt_new();

    const int num_threads = 8;
    const int allocations_per_thread = 100;

    // MSVC doesn't support VLAs - use heap allocation
    Thread* threads = (Thread*)malloc(num_threads * sizeof(Thread));
    ASSERT_NE(threads, NULL);
    ThreadTestData* thread_data = (ThreadTestData*)malloc(num_threads * sizeof(ThreadTestData));
    ASSERT_NE(thread_data, NULL);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].arena_mt = arena_mt;
        thread_data[i].thread_id = i;
        thread_data[i].num_allocations = allocations_per_thread;
        atomic_store(&thread_data[i].errors, 0);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_create(&threads[i], thread_alloc_test, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_join(&threads[i]);
    }

    int total_errors = 0;
    for (int i = 0; i < num_threads; i++) {
        total_errors += atomic_load(&thread_data[i].errors);
    }

    ASSERT_EQ(total_errors, 0);

    free(threads);
    free(thread_data);
    arena_mt_destroy(arena_mt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, arena_mt_growth) {
    FlArenaMt* arena_mt = arena_mt_new();

    const int num_threads = 4;

    // MSVC doesn't support VLAs - use heap allocation
    Thread* threads = (Thread*)malloc(num_threads * sizeof(Thread));
    ASSERT_NE(threads, NULL);
    ThreadTestData* thread_data = (ThreadTestData*)malloc(num_threads * sizeof(ThreadTestData));
    ASSERT_NE(thread_data, NULL);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].arena_mt = arena_mt;
        thread_data[i].thread_id = i;
        thread_data[i].num_allocations = 5;
        atomic_store(&thread_data[i].errors, 0);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_create(&threads[i], thread_large_alloc_test, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_join(&threads[i]);
    }

    int total_errors = 0;
    for (int i = 0; i < num_threads; i++) {
        total_errors += atomic_load(&thread_data[i].errors);
    }

    ASSERT_EQ(total_errors, 0);

    free(threads);
    free(thread_data);
    arena_mt_destroy(arena_mt);
}

// =============================================================================
// High Contention Lock-Free Tests
// =============================================================================

typedef struct HighContentionTestData {
    FlArenaMt* arena_mt;
    int thread_id;
    int num_allocations;
    int allocation_size;
    _Atomic u32 errors;
    _Atomic u32 allocation_count;
    _Atomic u32 completion_count;
} HighContentionTestData;

static void* high_contention_alloc_test(void* arg) {
    HighContentionTestData* data = (HighContentionTestData*)arg;
    FlArenaMt* arena_mt = data->arena_mt;

    for (int i = 0; i < data->num_allocations; i++) {
        // Variable sizes to increase contention
        size_t size = data->allocation_size + (i % 128);
        u8 pattern = (u8)((data->thread_id << 4) | (i & 0xF));

        void* ptr = arena_mt_alloc_raw(arena_mt, size, 8);
        if (!ptr) {
            atomic_fetch_add(&data->errors, 1);
            continue;
        }

        // Just count allocations (don't store pointers to avoid race condition)
        atomic_fetch_add(&data->allocation_count, 1);

        fill_memory_pattern(ptr, size, pattern);

        if (!check_memory_pattern(ptr, size, pattern)) {
            atomic_fetch_add(&data->errors, 1);
        }

        // Small delay to create more race conditions
        for (volatile int j = 0; j < 10; j++)
            ;
    }

    atomic_fetch_add(&data->completion_count, 1);
    return NULL;
}

static void* rapid_small_alloc_test(void* arg) {
    HighContentionTestData* data = (HighContentionTestData*)arg;
    FlArenaMt* arena_mt = data->arena_mt;

    // Rapid-fire small allocations to maximize CAS contention
    for (int i = 0; i < data->num_allocations; i++) {
        size_t size = 8 + (i % 32);
        void* ptr = arena_mt_alloc_raw(arena_mt, size, 4);

        if (!ptr) {
            atomic_fetch_add(&data->errors, 1);
            continue;
        }

        *(u32*)ptr = (u32)((data->thread_id << 16) | i);

        if (*(u32*)ptr != (u32)((data->thread_id << 16) | i)) {
            atomic_fetch_add(&data->errors, 1);
        }

        atomic_fetch_add(&data->allocation_count, 1);
    }

    atomic_fetch_add(&data->completion_count, 1);
    return NULL;
}

static void* mixed_size_contention_test(void* arg) {
    HighContentionTestData* data = (HighContentionTestData*)arg;
    FlArenaMt* arena_mt = data->arena_mt;

    for (int i = 0; i < data->num_allocations; i++) {
        size_t size;
        if (i % 3 == 0) {
            size = 8 + (i % 64);
        } else if (i % 3 == 1) {
            size = 1024 + (i % 2048);
        } else {
            size = KB(64) + (i % KB(32));
        }

        void* ptr = arena_mt_alloc_raw(arena_mt, size, 16);
        if (!ptr) {
            atomic_fetch_add(&data->errors, 1);
            continue;
        }

        u8* bytes = (u8*)ptr;
        u8 pattern = (u8)(data->thread_id + i);

        bytes[0] = pattern;
        if (size > 64)
            bytes[64] = pattern;
        if (size > 1024)
            bytes[1024] = pattern;
        bytes[size - 1] = pattern;

        if (bytes[0] != pattern || bytes[size - 1] != pattern) {
            atomic_fetch_add(&data->errors, 1);
        }

        atomic_fetch_add(&data->allocation_count, 1);
    }

    atomic_fetch_add(&data->completion_count, 1);
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, high_contention_lock_free) {
    FlArenaMt* arena_mt = arena_mt_new();

    const int num_threads = 32;
    const int allocations_per_thread = 1000;

    // MSVC doesn't support VLAs - use heap allocation
    Thread* threads = (Thread*)malloc(num_threads * sizeof(Thread));
    ASSERT_NE(threads, NULL);
    HighContentionTestData* thread_data
        = (HighContentionTestData*)calloc((size_t)num_threads, sizeof(HighContentionTestData));
    ASSERT_NE(thread_data, NULL);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].arena_mt = arena_mt;
        thread_data[i].thread_id = i;
        thread_data[i].num_allocations = allocations_per_thread;
        thread_data[i].allocation_size = 256;
        atomic_store(&thread_data[i].errors, 0);
        atomic_store(&thread_data[i].allocation_count, 0);
        atomic_store(&thread_data[i].completion_count, 0);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_create(&threads[i], high_contention_alloc_test, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_join(&threads[i]);
    }

    int total_errors = 0;
    int total_allocations = 0;
    int total_completions = 0;

    for (int i = 0; i < num_threads; i++) {
        total_errors += atomic_load(&thread_data[i].errors);
        total_allocations += atomic_load(&thread_data[i].allocation_count);
        total_completions += atomic_load(&thread_data[i].completion_count);
    }

    ASSERT_EQ(total_errors, 0);
    ASSERT_EQ(total_completions, num_threads);
    ASSERT_GE(total_allocations, num_threads * allocations_per_thread * 0.95); // Allow for some variance

    free(threads);
    free(thread_data);
    arena_mt_destroy(arena_mt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, rapid_small_allocations) {
    FlArenaMt* arena_mt = arena_mt_new();

    const int num_threads = 16;
    const int allocations_per_thread = 5000;

    // MSVC doesn't support VLAs - use heap allocation
    Thread* threads = (Thread*)malloc(num_threads * sizeof(Thread));
    ASSERT_NE(threads, NULL);
    HighContentionTestData* thread_data
        = (HighContentionTestData*)calloc((size_t)num_threads, sizeof(HighContentionTestData));
    ASSERT_NE(thread_data, NULL);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].arena_mt = arena_mt;
        thread_data[i].thread_id = i;
        thread_data[i].num_allocations = allocations_per_thread;
        atomic_store(&thread_data[i].errors, 0);
        atomic_store(&thread_data[i].allocation_count, 0);
        atomic_store(&thread_data[i].completion_count, 0);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_create(&threads[i], rapid_small_alloc_test, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_join(&threads[i]);
    }

    int total_errors = 0;
    int total_allocations = 0;

    for (int i = 0; i < num_threads; i++) {
        total_errors += atomic_load(&thread_data[i].errors);
        total_allocations += atomic_load(&thread_data[i].allocation_count);
    }

    ASSERT_EQ(total_errors, 0);
    ASSERT_EQ(total_allocations, num_threads * allocations_per_thread);

    free(threads);
    free(thread_data);
    arena_mt_destroy(arena_mt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, mixed_size_high_contention) {
    FlArenaMt* arena_mt = arena_mt_new();

    const int num_threads = 24;
    const int allocations_per_thread = 500;

    // MSVC doesn't support VLAs - use heap allocation
    Thread* threads = (Thread*)malloc(num_threads * sizeof(Thread));
    ASSERT_NE(threads, NULL);
    HighContentionTestData* thread_data
        = (HighContentionTestData*)calloc((size_t)num_threads, sizeof(HighContentionTestData));
    ASSERT_NE(thread_data, NULL);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].arena_mt = arena_mt;
        thread_data[i].thread_id = i;
        thread_data[i].num_allocations = allocations_per_thread;
        atomic_store(&thread_data[i].errors, 0);
        atomic_store(&thread_data[i].allocation_count, 0);
        atomic_store(&thread_data[i].completion_count, 0);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_create(&threads[i], mixed_size_contention_test, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        os_thread_join(&threads[i]);
    }

    int total_errors = 0;
    int total_allocations = 0;

    for (int i = 0; i < num_threads; i++) {
        total_errors += atomic_load(&thread_data[i].errors);
        total_allocations += atomic_load(&thread_data[i].allocation_count);
    }

    ASSERT_EQ(total_errors, 0);
    ASSERT_EQ(total_allocations, num_threads * allocations_per_thread);

    arena_mt_destroy(arena_mt);

    free(thread_data);
    free(threads);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena, stress_lock_free_rounds) {
    const int num_rounds = 50;
    const int threads_per_round = 16;
    const int allocs_per_thread = 100;

    for (int round = 0; round < num_rounds; round++) {
        FlArenaMt* arena_mt = arena_mt_new();

        // MSVC doesn't support VLAs - use heap allocation
        Thread* threads = (Thread*)malloc(threads_per_round * sizeof(Thread));
        ASSERT_NE(threads, NULL);
        HighContentionTestData* thread_data
            = (HighContentionTestData*)malloc(threads_per_round * sizeof(HighContentionTestData));
        ASSERT_NE(thread_data, NULL);

        for (int i = 0; i < threads_per_round; i++) {
            thread_data[i].arena_mt = arena_mt;
            thread_data[i].thread_id = i;
            thread_data[i].num_allocations = allocs_per_thread;
            thread_data[i].allocation_size = 128 + (round % 256);
            atomic_store(&thread_data[i].errors, 0);
            atomic_store(&thread_data[i].allocation_count, 0);
            atomic_store(&thread_data[i].completion_count, 0);
        }

        for (int i = 0; i < threads_per_round; i++) {
            os_thread_create(&threads[i], high_contention_alloc_test, &thread_data[i]);
        }

        for (int i = 0; i < threads_per_round; i++) {
            os_thread_join(&threads[i]);
        }

        int round_errors = 0;
        for (int i = 0; i < threads_per_round; i++) {
            round_errors += atomic_load(&thread_data[i].errors);
        }

        if (round_errors > 0) {
            ASSERT_EQ(round_errors, 0);
        }

        free(threads);
        free(thread_data);
        arena_mt_destroy(arena_mt);
    }
}
