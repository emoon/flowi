#include "utest.h"
#include "../src/handles.h"
#include "../src/allocator.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define HANDLE_SHIFT (32)
#define HANDLE_BITS (1LL << HANDLE_SHIFT)
#define HANDLE_MASK (HANDLE_BITS - 1)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 handle_index(u64 handle) {
	return (u32)((handle) >> HANDLE_SHIFT);
}

FL_INLINE u32 handle_inner(u64 handle) {
	return (u32)((handle) & HANDLE_MASK);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int s_count_malloc_fail = 0;

void* dummy_alloc_count(void* user_data, u64 size) {
	FL_UNUSED(user_data);
	if (s_count_malloc_fail <= 0) {
		return NULL;
	}

	s_count_malloc_fail--;
	return malloc(size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* realloc_null(void* user_data, void* ptr, u64 size) {
	FL_UNUSED(user_data);
	FL_UNUSED(ptr);
	FL_UNUSED(size);
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Malloc based allocator. We should use tslf or similar in a sandbox, but this is atleast in one place

static void* alloc_malloc(void* user_data, u64 size) {
    FL_UNUSED(user_data);
    return malloc(size);
}

static void* realloc_malloc(void* user_data, void* ptr, u64 size) {
    FL_UNUSED(user_data);
    return realloc(ptr, size);
}

static void free_malloc(void* user_data, void* ptr) {
    FL_UNUSED(user_data);
    free(ptr);
}

static FlAllocator malloc_allocator = {
    alloc_malloc, NULL, realloc_malloc, free_malloc, NULL,
};

static FlAllocator malloc_alloc_count_fail_allocator = {
    dummy_alloc_count, NULL, realloc_malloc, free_malloc, NULL,
};

static FlAllocator malloc_realloc_fail_allocator = {
    alloc_malloc, NULL, realloc_null, free_malloc, NULL,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_destroy) {
	Handles handles;
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_allocator, 2, 8));
	Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_alloc_fail_destroy) {
	s_count_malloc_fail = 0;
	Handles handles;
	ASSERT_FALSE(Handles_create_impl(&handles, &malloc_alloc_count_fail_allocator, 2, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_destroy_validate_init) {
	Handles handles;
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_allocator, 2, 8));

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
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_allocator, 4, 8));

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
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_allocator, 4, 8));

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
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_allocator, 1, 8));

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

UTEST(Handles, test_realloc_fail) {
	Handles handles;
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_realloc_fail_allocator, 1, 8));

	ASSERT_EQ(1, handles.capacity);
	ASSERT_NE(NULL, Handles_create_handle(&handles));
	ASSERT_EQ(NULL, Handles_create_handle(&handles));

	Handles_destroy(&handles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Handles, create_remove_1) {
	Handles handles;
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_allocator, 4, 8));

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
	ASSERT_TRUE(Handles_create_impl(&handles, &malloc_allocator, 4, 8));

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


