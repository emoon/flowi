#include "../src/allocator.h"
#include "../src/handles.h"
#include "utest.h"

extern FlAllocator g_malloc_allocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define HANDLE_SHIFT (32)
#define HANDLE_BITS (1LL << HANDLE_SHIFT)
#define HANDLE_MASK (HANDLE_BITS - 1)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 handle_index(u64 handle) {
    return (u32)((handle) >> HANDLE_SHIFT);
}

FL_INLINE u32 handle_inner(u64 handle) {
    return (u32)((handle)&HANDLE_MASK);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_destroy) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 2, 8));
    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_destroy_validate_init) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 2, 8));

    ASSERT_EQ(handles.len, 0);
    ASSERT_EQ(handles.capacity, 2);
    ASSERT_EQ(handles.object_size, 8);
    ASSERT_EQ(handles.free_slots_count, 0);
    ASSERT_EQ(handles.next_inner_id, 1);

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_handles) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 4, 8));

    u64 h = *(u64*)Handles_create_handle(&handles);

    ASSERT_EQ(handle_index(h), 0);
    ASSERT_EQ(handle_inner(h), 1);
    ASSERT_EQ(handles.len, 1);

    h = *(u64*)Handles_create_handle(&handles);

    ASSERT_EQ(handle_index(h), 1);
    ASSERT_EQ(handle_inner(h), 2);
    ASSERT_EQ(handles.len, 2);

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_handles_2) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 4, 8));

    u64 h = *(u64*)Handles_create_handle(&handles);

    ASSERT_EQ(handle_index(h), 0);
    ASSERT_EQ(handle_inner(h), 1);
    ASSERT_EQ(handles.len, 1);

    h = *(u64*)Handles_create_handle(&handles);

    ASSERT_EQ(handle_index(h), 1);
    ASSERT_EQ(handle_inner(h), 2);
    ASSERT_EQ(handles.len, 2);

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, test_realloc) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 1, 8));

    ASSERT_EQ(1, handles.capacity);
    Handles_create_handle(&handles);
    Handles_create_handle(&handles);

    ASSERT_EQ(2, handles.capacity);
    Handles_create_handle(&handles);
    Handles_create_handle(&handles);

    ASSERT_EQ(4, handles.capacity);

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_remove_1) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 4, 8));

    u64 h = *(u64*)Handles_create_handle(&handles);
    ASSERT_EQ(1, handles.len);
    ASSERT_EQ(0, handles.free_slots_count);

    Handles_remove_handle(&handles, h);
    ASSERT_EQ(1, handles.len);
    ASSERT_EQ(1, handles.free_slots_count);
    ASSERT_EQ(0, handles.free_slots[0]);

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_remove_2) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 4, 8));

    Handles_create_handle(&handles);
    u64 h = *(u64*)Handles_create_handle(&handles);

    ASSERT_EQ(2, handles.len);
    ASSERT_EQ(0, handles.free_slots_count);

    Handles_remove_handle(&handles, h);

    ASSERT_EQ(1, handles.free_slots_count);
    ASSERT_EQ(1, handles.free_slots[0]);

    u64 h0 = *(u64*)Handles_create_handle(&handles);

    ASSERT_EQ(2, handles.len);
    ASSERT_EQ(1, handle_index(h0));
    ASSERT_EQ(0, handles.free_slots_count);

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, test_valid_handle) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 4, 8));

    Handles_create_handle(&handles);
    u64 h = *(u64*)Handles_create_handle(&handles);

    ASSERT_TRUE(Handles_is_valid(&handles, h));

    Handles_remove_handle(&handles, h);

    ASSERT_FALSE(Handles_is_valid(&handles, h));

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, test_remove_invalid) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 4, 8));

    u64 h = *(u64*)Handles_create_handle(&handles);
    Handles_remove_handle(&handles, h | 0x111);

    ASSERT_EQ(1, handles.len);
    ASSERT_EQ(0, handles.free_slots_count);

    Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, test_get_data) {
    Handles handles;
    ASSERT_TRUE(Handles_create_impl(&handles, &g_malloc_allocator, 4, 8));

    u64* d0 = Handles_create_handle(&handles);
    u64 h0 = *d0;

    u64* d1 = Handles_create_handle(&handles);
    u64 h1 = *d1;

    ASSERT_EQ(d0, Handles_get_data(&handles, h0));
    ASSERT_EQ(d1, Handles_get_data(&handles, h1));
    ASSERT_EQ(NULL, Handles_get_data(&handles, h0 | 0x333));
    ASSERT_EQ(NULL, Handles_get_data(&handles, h1 | 0x222));

    Handles_destroy(&handles);
}
