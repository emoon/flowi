#include <assert.h>
#include "../src/internal.h"
#include "../src/linear_allocator.h"
#include "utest.h"

extern FlAllocator g_malloc_allocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void allocator_create(LinearAllocator* alloc, const char* name, int size) {
    u8* data = malloc(size);
    memset(data, 0xcd, size);  // init memory to cdcd for uncleared memory
    LinearAllocator_create(alloc, name, data, size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void allocator_destroy(LinearAllocator* allocator) {
    free(allocator->start_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, init) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test_init", 20);
    ASSERT_TRUE(strcmp(alloc.id, "test_init") == 0);
    ASSERT_TRUE(alloc.start_data == alloc.start_data);
    ASSERT_TRUE(alloc.end_data == alloc.start_data + 20);
    ASSERT_TRUE(alloc.current_data == alloc.start_data);
    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, alloc_single_byte) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);
    u8* data = LinearAllocator_alloc(&alloc, u8);

    // make sure we have incremented pointers enough
    ASSERT_TRUE(alloc.current_data == data + 1);
    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, alloc_single_byte_unaligned) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);
    u8* data = LinearAllocator_alloc_unaligend(&alloc, u8);

    // make sure we have incremented pointers enough
    ASSERT_TRUE(alloc.current_data == data + 1);
    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, rewind) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);

    // allocate some data
    LinearAllocator_alloc(&alloc, u8);
    LinearAllocator_alloc(&alloc, u16);
    LinearAllocator_alloc_array(&alloc, u16, 2);

    LinearAllocator_rewind(&alloc);

    // Make sure we are back at the start after rewind
    ASSERT_TRUE(alloc.current_data == alloc.start_data);
    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, out_of_memory) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);
    u8* data = LinearAllocator_alloc_array(&alloc, u8, 21);
    ASSERT_TRUE(data == NULL);
    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, memory_pattern) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);
    u8* data = LinearAllocator_alloc_array(&alloc, u8, 2);
    u8* data_2 = LinearAllocator_alloc_array_zero(&alloc, u8, 2);

    ASSERT_TRUE(data[0] == 0xcd);
    ASSERT_TRUE(data[1] == 0xcd);

    ASSERT_TRUE(data_2[0] == 0x00);
    ASSERT_TRUE(data_2[1] == 0x00);

    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, alignment) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);

    // allocate until we have an uneven pointer
    for (int i = 0; i < 8; ++i) {
        u8* data = LinearAllocator_alloc(&alloc, u8);
        if (((uintptr_t)data) & 1) {
            break;
        }
    }

    // Allocate a u64 value that should be 8 byte alinged
    u64* data = LinearAllocator_alloc(&alloc, u64);

    // make sure we have aligned pointer
    ASSERT_TRUE((((uintptr_t)data) & 0x7) == 0);

    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, size_left) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);

    ASSERT_EQ(LinearAllocator_memory_left(&alloc), 20);
    LinearAllocator_alloc(&alloc, u64);
    ASSERT_EQ(LinearAllocator_memory_left(&alloc), 20 - 8);

    allocator_destroy(&alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, update_resize) {
    LinearAllocator alloc;
    allocator_create(&alloc, "test", 20);

    ASSERT_EQ(LinearAllocator_current_position(&alloc), 0);
    ASSERT_EQ(LinearAllocator_memory_left(&alloc), 20);

    LinearAllocator_alloc(&alloc, u64);

    free(alloc.start_data);

    LinearAllocator_update_resize(&alloc, 0, 40);

    ASSERT_EQ(LinearAllocator_current_position(&alloc), 8);
    ASSERT_EQ(LinearAllocator_memory_left(&alloc), 40 - 8);

    allocator_destroy(&alloc);
}
