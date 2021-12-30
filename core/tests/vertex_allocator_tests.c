#include <assert.h>
#include "../src/internal.h"
#include "../src/linear_allocator.h"
#include "../src/vertex_allocator.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test that a regular setup with malloc works as expected

UTEST(VertexAllocator, default_create) {
	VertexAllocator vertex_allocator;

	int vertex_sizes[VertexAllocType_SIZEOF] = { 256, 256 };
	int index_sizes[VertexAllocType_SIZEOF] = { 256, 256 };

	ASSERT_TRUE(VertexAllocator_create(&vertex_allocator, &malloc_allocator, vertex_sizes, index_sizes, false));
	VertexAllocator_destroy(&vertex_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, default_create_resize) {
	VertexAllocator vertex_allocator;

	int vertex_sizes[VertexAllocType_SIZEOF] = { 256, 256 };
	int index_sizes[VertexAllocType_SIZEOF] = { 256, 256 };

	ASSERT_TRUE(VertexAllocator_create(&vertex_allocator, &malloc_allocator, vertex_sizes, index_sizes, true));
	VertexAllocator_destroy(&vertex_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, end_frame) {
	VertexAllocator vertex_allocator;

	int vertex_sizes[VertexAllocType_SIZEOF] = { 256, 256 };
	int index_sizes[VertexAllocType_SIZEOF] = { 256, 256 };

	ASSERT_TRUE(VertexAllocator_create(&vertex_allocator, &malloc_allocator, vertex_sizes, index_sizes, true));

	u8* verts = NULL;
	u8* indices = NULL;

	u8* verts_2 = NULL;
	u8* indices_2 = NULL;

	u8* verts_3 = NULL;
	u8* indices_3 = NULL;

	// Alloc data and expect to get valid pointers back

	ASSERT_TRUE(VertexAllocator_alloc(&vertex_allocator, VertexAllocType_PosColor, &verts, &indices, 4, 2));
	ASSERT_NE(verts, NULL);
	ASSERT_NE(indices, NULL);

	// End frame and we expect new allocations to not have the same pointers
	VertexAllocator_end_frame(&vertex_allocator);

	ASSERT_TRUE(VertexAllocator_alloc(&vertex_allocator, VertexAllocType_PosColor, &verts_2, &indices_2, 4, 2));
	ASSERT_NE(verts, verts_2);
	ASSERT_NE(indices, indices_2);

	// end frame until we get back to the starting point
	for (int i = 0; i < FL_FRAME_HISTORY - 1; ++i) {
		VertexAllocator_end_frame(&vertex_allocator);
	}

	// Expect these ptrs to be the same as the first ones at this point
	ASSERT_TRUE(VertexAllocator_alloc(&vertex_allocator, VertexAllocType_PosColor, &verts_3, &indices_3, 4, 2));
	ASSERT_EQ(verts, verts_3);
	ASSERT_EQ(indices, indices_3);

	VertexAllocator_destroy(&vertex_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* dummy_alloc_1(void* user_data, u64 count) {
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, fail_create) {
	VertexAllocator vertex_allocator;

	FlAllocator allocator = { 0 };
	allocator.alloc = dummy_alloc_1;

	int vertex_sizes[VertexAllocType_SIZEOF] = { 256, 256 };
	int index_sizes[VertexAllocType_SIZEOF] = { 256, 256 };

	// As the allocator can't allocate any memory we expect this call to fail
	ASSERT_FALSE(VertexAllocator_create(&vertex_allocator, &allocator, vertex_sizes, index_sizes, false));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int s_count = 0;

static void* dummy_alloc_2(void* user_data, u64 count) {
	if (s_count++ == 0) {
		return (void*)1;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, fail_create_2) {
	VertexAllocator vertex_allocator;

	FlAllocator allocator = { 0 };
	allocator.alloc = dummy_alloc_2;

	int vertex_sizes[VertexAllocType_SIZEOF] = { 256, 256 };
	int index_sizes[VertexAllocType_SIZEOF] = { 256, 256 };

	// As the allocator can't allocate any memory for the second call we expect this to fail
	ASSERT_FALSE(VertexAllocator_create(&vertex_allocator, &allocator, vertex_sizes, index_sizes, false));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* dummy_alloc_3(void* user_data, u64 count) {
	return (void*)1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, fail_alloc) {
	VertexAllocator vertex_allocator;

	FlAllocator allocator = { 0 };
	allocator.alloc = dummy_alloc_3;

	int vertex_sizes[VertexAllocType_SIZEOF] = { 10, 10 };
	int index_sizes[VertexAllocType_SIZEOF] = { 10, 10 };

	// As the allocator can't allocate any memory for the second call we expect this to fail
	VertexAllocator_create(&vertex_allocator, &allocator, vertex_sizes, index_sizes, false);

	u8* verts = NULL;
	u8* indices = NULL;

	// Expect the allocation to fail due to small amount of data and no resize
	ASSERT_FALSE(VertexAllocator_alloc(&vertex_allocator, VertexAllocType_PosColor, &verts, &indices, 256, 256));
	ASSERT_EQ(verts, NULL);
	ASSERT_EQ(indices, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* dummy_realloc(void* user_data, void* ptr, u64 count) {
	return (void*)4;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, fail_alloc_realloc_ok) {
	VertexAllocator vertex_allocator;

	FlAllocator allocator = { 0 };
	allocator.alloc = dummy_alloc_3;
	allocator.realloc = dummy_realloc;

	int vertex_sizes[VertexAllocType_SIZEOF] = { 10, 10 };
	int index_sizes[VertexAllocType_SIZEOF] = { 10, 10 };

	// As the allocator can't allocate any memory for the second call we expect this to fail
	VertexAllocator_create(&vertex_allocator, &allocator, vertex_sizes, index_sizes, true);

	u8* verts = NULL;
	u8* indices = NULL;

	// Expect the allocation to fail due to small amount of data and no resize
	ASSERT_TRUE(VertexAllocator_alloc(&vertex_allocator, VertexAllocType_PosColor, &verts, &indices, 14, 14));
	ASSERT_EQ((uintptr_t)verts, 4);
	ASSERT_EQ((uintptr_t)indices, 4);
}



