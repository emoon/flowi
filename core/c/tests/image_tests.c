#include "utest.h"
#include <flowi_core/image.h>
#include "../src/internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_ok) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* flowi_ctx = fl_context_create(state);

	FlImageId id = fl_image_new_from_file("data/uv.png");
	ASSERT_NE(0, id);

	fl_image_destroy(id);
    fl_context_destroy(flowi_ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_fail) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* flowi_ctx = fl_context_create(state);

	FlImageId id = fl_image_new_from_file("data/no_such_file.png");
	ASSERT_EQ(0, id);

	fl_image_destroy(id);
    fl_context_destroy(flowi_ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_from_memory) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* flowi_ctx = fl_context_create(state);

    u32 size = 0;
    u8* data = Io_load_file_to_memory("data/uv.png", &size);
    ASSERT_NE(NULL, data);

	FlImageId id = fl_image_load_from_memory("uv", data, size);
	ASSERT_NE(0, id);

	free(data);	// TODO: fix me, custom allocator

	fl_image_destroy(id);
    fl_context_destroy(flowi_ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_get_data_ok) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* flowi_ctx = fl_context_create(state);

	FlImageId id = fl_image_new_from_file("data/uv.png");
	ASSERT_NE(0, id);

	FlImage data = fl_image_get_image_data(id);
	ASSERT_EQ(512, data.width);
	ASSERT_EQ(512, data.height);

	fl_image_destroy(id);
    fl_context_destroy(flowi_ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_get_invalid_data) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* flowi_ctx = fl_context_create(state);

	FlImageId id = fl_image_new_from_file("data/uv.png");
	ASSERT_NE(0, id);

	fl_image_destroy(id);
	FlImage data = fl_image_get_image_data(id);

	ASSERT_EQ(0, data.width);
	ASSERT_EQ(0, data.height);

	fl_image_destroy(id);
    fl_context_destroy(flowi_ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_get_invalid_data_2) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* flowi_ctx = fl_context_create(state);

	FlImageId id = fl_image_new_from_file("data/no_such_file.png");
	ASSERT_EQ(0, id);

	FlImage data = fl_image_get_image_data(id);
	ASSERT_EQ(0, data.width);
	ASSERT_EQ(0, data.height);

	fl_image_destroy(id);
    fl_context_destroy(flowi_ctx);
    fl_destroy(state);
}


