#include "utest.h"
#include "../src/area.h"
#include "../src/style.h"

#ifdef _WIN32
#include <malloc.h> // alloca
#endif

struct FlContext;

extern struct FlContext* g_ctx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate a basic area

UTEST(Area, area_default) {
	FlStyle* style = fl_style_get_default(g_ctx);

	u32 background_color = FL_RGB(255, 0, 255);

	FlVec2 size = { 10.0f, 20.0f };

	style->background_color = background_color;

	Area* area = Area_generate(g_ctx, style, size);

	// Validate vertex counts are correct
	ASSERT_EQ(area->vertex_count, 4);
	ASSERT_EQ(area->index_count, 6);

	ASSERT_EQ(area->index_buffer[0], 0);
	ASSERT_EQ(area->index_buffer[1], 1);
	ASSERT_EQ(area->index_buffer[2], 2);
	ASSERT_EQ(area->index_buffer[3], 0);
	ASSERT_EQ(area->index_buffer[4], 2);
	ASSERT_EQ(area->index_buffer[5], 3);

	ASSERT_EQ(area->vertex_buffer[0].color, background_color);
	ASSERT_EQ(area->vertex_buffer[1].color, background_color);
	ASSERT_EQ(area->vertex_buffer[2].color, background_color);
	ASSERT_EQ(area->vertex_buffer[3].color, background_color);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Genererate triangle list for corner

UTEST(Area, count_triangle_list) {
	int count = Area_generate_corner_triangle_list(NULL, 0, 3);
	ASSERT_EQ(count, (3 - 1) * 3);

	count = Area_generate_corner_triangle_list(NULL, 0, 6);
	ASSERT_EQ(count, (6 - 1) * 3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Area, verify_triangle_list) {
	int vert_count = 4;
	FlIdxSize* index_list = alloca(vert_count * 3 * sizeof(FlIdxSize));

	int count = Area_generate_corner_triangle_list(index_list, 0, vert_count);
	ASSERT_EQ(count, (vert_count - 1) * 3);

	ASSERT_EQ(index_list[0], 2);
	ASSERT_EQ(index_list[1], 4);
	ASSERT_EQ(index_list[2], 0);

	ASSERT_EQ(index_list[3], 1);
	ASSERT_EQ(index_list[4], 2);
	ASSERT_EQ(index_list[5], 0);

	ASSERT_EQ(index_list[6], 3);
	ASSERT_EQ(index_list[7], 4);
	ASSERT_EQ(index_list[8], 2);
}

