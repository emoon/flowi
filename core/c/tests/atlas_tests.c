#include "utest.h"
#include "../src/atlas.h"
#include "../src/allocator.h"
#include "../src/internal.h"

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
	FlGlobalState* state = fl_create(NULL);
	struct Atlas* atlas = Atlas_create(256, 256, AtlasImageType_U8, state, &malloc_allocator);
	Atlas_destroy(atlas);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, add_single_rect) {
	FlGlobalState* state = fl_create(NULL);
	int rx = 0, ry = 0, stride = 0;

	struct Atlas* atlas = Atlas_create(256, 256, AtlasImageType_U8, state, &malloc_allocator);
	ASSERT_NE(Atlas_add_rect(atlas, 64, 64, &rx, &ry, &stride), NULL);

	Atlas_destroy(atlas);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, add_single_rect_fail) {
	int rx = 0, ry = 0, stride = 0;
	FlGlobalState* state = fl_create(NULL);

	struct Atlas* atlas = Atlas_create(64, 64, AtlasImageType_U8, state, &malloc_allocator);
	ASSERT_EQ(Atlas_add_rect(atlas, 128, 128, &rx, &ry, &stride), NULL);

	Atlas_destroy(atlas);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, add_rects_util_fail_expand_once) {
	int rx = 0, ry = 0, stride = 0, i;
	FlGlobalState* state = fl_create(NULL);

	struct Atlas* atlas = Atlas_create(64, 64, AtlasImageType_U8, state, &malloc_allocator);

	for (i = 0; i < 128; ++i) {
		if (Atlas_add_rect(atlas, i, i * 2, &rx, &ry, &stride) == NULL) {
			break;
		}
	}

	ASSERT_TRUE(Atlas_expand(atlas, 256, 256));
	ASSERT_NE(Atlas_add_rect(atlas, i, i * 2, &rx, &ry, &stride), NULL);

	Atlas_destroy(atlas);
	fl_destroy(state);
}

