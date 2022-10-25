#include "../src/allocator.h"
#include "../src/atlas.h"
#include "../src/internal.h"
#include "utest.h"

extern FlAllocator g_malloc_allocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, create_destroy) {
    FlGlobalState* state = fl_create(NULL);
    struct Atlas* atlas = Atlas_create(256, 256, AtlasImageType_U8, state, &g_malloc_allocator);
    Atlas_destroy(atlas);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, add_single_rect) {
    FlGlobalState* state = fl_create(NULL);
    int rx = 0, ry = 0, stride = 0;

    struct Atlas* atlas = Atlas_create(256, 256, AtlasImageType_U8, state, &g_malloc_allocator);
    ASSERT_NE(Atlas_add_rect(atlas, 64, 64, &rx, &ry, &stride), NULL);

    Atlas_destroy(atlas);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, add_single_rect_fail) {
    int rx = 0, ry = 0, stride = 0;
    FlGlobalState* state = fl_create(NULL);

    struct Atlas* atlas = Atlas_create(64, 64, AtlasImageType_U8, state, &g_malloc_allocator);
    ASSERT_EQ(Atlas_add_rect(atlas, 128, 128, &rx, &ry, &stride), NULL);

    Atlas_destroy(atlas);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Atlas, add_rects_util_fail_expand_once) {
    int rx = 0, ry = 0, stride = 0, i;
    FlGlobalState* state = fl_create(NULL);

    struct Atlas* atlas = Atlas_create(64, 64, AtlasImageType_U8, state, &g_malloc_allocator);

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
