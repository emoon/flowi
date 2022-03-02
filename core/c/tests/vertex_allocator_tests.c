#include <assert.h>
#include "../src/internal.h"
#include "../src/linear_allocator.h"
#include "../src/vertex_allocator.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern FlAllocator g_malloc_allocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test that a regular setup with malloc works as expected

UTEST(VertexAllocator, default_create) {
    VertexAllocator vertex_allocator;

    int vertex_sizes[VertexAllocType_SIZEOF] = {256, 256};
    int index_sizes[VertexAllocType_SIZEOF] = {256, 256};

    ASSERT_TRUE(VertexAllocator_create(&vertex_allocator, &g_malloc_allocator, vertex_sizes, index_sizes, false));
    VertexAllocator_destroy(&vertex_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, default_create_resize) {
    VertexAllocator vertex_allocator;

    int vertex_sizes[VertexAllocType_SIZEOF] = {256, 256};
    int index_sizes[VertexAllocType_SIZEOF] = {256, 256};

    ASSERT_TRUE(VertexAllocator_create(&vertex_allocator, &g_malloc_allocator, vertex_sizes, index_sizes, true));
    VertexAllocator_destroy(&vertex_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(VertexAllocator, end_frame) {
    VertexAllocator vertex_allocator;

    int vertex_sizes[VertexAllocType_SIZEOF] = {256, 256};
    int index_sizes[VertexAllocType_SIZEOF] = {256, 256};

    ASSERT_TRUE(VertexAllocator_create(&vertex_allocator, &g_malloc_allocator, vertex_sizes, index_sizes, true));

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

UTEST(VertexAllocator, get_counts) {
    VertexAllocator vertex_allocator;

    int vertex_sizes[VertexAllocType_SIZEOF] = {256, 256};
    int index_sizes[VertexAllocType_SIZEOF] = {256, 256};

    ASSERT_TRUE(VertexAllocator_create(&vertex_allocator, &g_malloc_allocator, vertex_sizes, index_sizes, false));

    FlVertPosColor* verts_0 = NULL;
    FlIdxSize* indices_0 = NULL;

    ASSERT_TRUE(VertexAllocator_alloc_pos_color(&vertex_allocator, &verts_0, &indices_0, 4, 6));
    VertsCounts counts_0 = VertexAllocator_get_pos_color_counts(&vertex_allocator);

    ASSERT_NE(verts_0, NULL);
    ASSERT_NE(indices_0, NULL);
    ASSERT_EQ(counts_0.vertex_count, 4);
    ASSERT_EQ(counts_0.index_count, 6);

    FlVertPosUvColor* verts_1 = NULL;
    FlIdxSize* indices_1 = NULL;

    ASSERT_TRUE(VertexAllocator_alloc_pos_uv_color(&vertex_allocator, &verts_1, &indices_1, 8, 12));
    VertsCounts counts_1 = VertexAllocator_get_pos_uv_color_counts(&vertex_allocator);
    ASSERT_NE(verts_1, NULL);
    ASSERT_NE(indices_1, NULL);
    ASSERT_EQ(counts_1.vertex_count, 8);
    ASSERT_EQ(counts_1.index_count, 12);

    ASSERT_TRUE(VertexAllocator_alloc_pos_color(&vertex_allocator, &verts_0, &indices_0, 2, 4));
    VertsCounts counts_2 = VertexAllocator_get_pos_color_counts(&vertex_allocator);
    ASSERT_NE(verts_0, NULL);
    ASSERT_NE(indices_0, NULL);
    ASSERT_EQ(counts_2.vertex_count, 6);
    ASSERT_EQ(counts_2.index_count, 10);

    VertexAllocator_destroy(&vertex_allocator);
}
