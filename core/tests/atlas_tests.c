#include "utest.h"
#include "../src/atlas.h"
#include "../src/allocator.h"

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, create_destroy) {
	struct Atlas* atlas = Atlas_create(256, 256, 256, &malloc_allocator);
	Atlas_destroy(atlas);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, add_single_rect) {
	int rx = 0, ry = 0;
	struct Atlas* atlas = Atlas_create(256, 256, 256, &malloc_allocator);

	ASSERT_TRUE(Atlas_add_rect(atlas, 64, 64, &rx, &ry));

	Atlas_destroy(atlas);
}

