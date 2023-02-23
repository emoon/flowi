#include <assert.h>
#include <stdlib.h>
#include "../src/allocator.h"
#include "../src/atlas.h"
#include "../src/internal.h"
#include "ubench.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* alloc_malloc(void* user_data, u64 size) {
    FL_UNUSED(user_data);
    return malloc(size);
}

void* realloc_malloc(void* user_data, void* ptr, u64 size) {
    FL_UNUSED(user_data);
    return realloc(ptr, size);
}

static void free_malloc(void* user_data, void* ptr) {
    FL_UNUSED(user_data);
    free(ptr);
}

static void memory_error(void* user_data, const char* text, int text_len) {
    FL_UNUSED(user_data);
    FL_UNUSED(text);
    FL_UNUSED(text_len);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlAllocator malloc_allocator = {
    FlAllocatorError_Exit, NULL, memory_error, alloc_malloc, NULL, realloc_malloc, free_malloc,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UBENCH(Atlas, add_rect_to_1000_with_malloc) {
    int rx = 0, ry = 0, stride = 0;

    FlGlobalState* state = fl_create(NULL);
    struct Atlas* atlas = Atlas_create(8192, 8192, 1024 * 10, state, &malloc_allocator);

    for (int i = 0; i < 1000; ++i) {
        Atlas_add_rect(atlas, rand() & 3, rand() & 3, &rx, &ry, &stride);
    }

    Atlas_destroy(atlas);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UBENCH(Atlas, add_rect_to_5000_with_malloc) {
    int rx = 0, ry = 0, stride = 0;
    FlGlobalState* state = fl_create(NULL);

    struct Atlas* atlas = Atlas_create(32000, 32000, 1024 * 100, state, &malloc_allocator);

    for (int i = 0; i < 5000; ++i) {
        Atlas_add_rect(atlas, rand() & 3, rand() & 3, &rx, &ry, &stride);
    }

    Atlas_destroy(atlas);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
