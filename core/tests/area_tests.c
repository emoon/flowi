#include "utest.h"
#include "../src/area.h"
#include "../src/style.h"

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

UTEST(Area, corner_triangle_list_1) {
	//Area_generate_corner_triangle_list(NULL, 0, 5);
}

