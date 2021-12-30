#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "area.h"
#include "style.h"
#include "internal.h"
#include "render.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Areas are used for generating an area around a widget/control.
// This can be the outline for a button, box, etc.
// The look rounding/colors/etc is determined by the current style that has been set
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate the sin values for a rounded corner

static int generate_corner_values(FlVec2* values, FlVec2 size, float corner_size, LengthPercentType type) {
	// first calculate the value the number of pixels

	const float half_x = size.x * 0.5f;
	const float half_y = size.y * 0.5f;
	const float shortest_side = half_x > half_y ? half_y : half_x;

	const int corner_pixels_percent = (int)(shortest_side * (corner_size * 0.01));
	const int corner_pixels = type == FlLengthPercentType_Length ? (int)shortest_side : corner_pixels_percent;

	// Test with generating every other pixel to see how it looks
	int pixel_count = corner_pixels;

	const float sin_step = (M_PI/2.0f) / pixel_count;
	float angle = 0.0f;

	// TODO: SIMD
	for (int i = 0; i < pixel_count; ++i) {
		values[i].x = (float)(cos(angle) * corner_pixels);
		values[i].y = (float)(sin(angle) * corner_pixels);
		angle += sin_step;
	}

	return corner_pixels;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void generate_recursive_count(int* count, int start_index, int end_index) {
	int middle_index = start_index + ((end_index - start_index) / 2);

	if (middle_index == start_index) {
		return;
	}

	*count += 3;

	generate_recursive_count(count, start_index, middle_index);
	generate_recursive_count(count, middle_index, end_index);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This will generate the triangle list for a corner. Instead of a fan we try to maximize the area each triangle covers
// This generator is a bit slower than doing a fan, but is more GPU friendly.

static void generate_recursive(FlIdxSize* index_list, int* offset, int start_index, int end_index) {
	int middle_index = start_index + ((end_index - start_index) / 2);

	if (middle_index == start_index) {
		return;
	}

	int i = *offset;

	index_list[i + 0] = middle_index;
	index_list[i + 1] = end_index;
	index_list[i + 2] = start_index;

	*offset += 3;

	generate_recursive(index_list, offset, start_index, middle_index);
	generate_recursive(index_list, offset, middle_index, end_index);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int Area_generate_corner_triangle_list(FlIdxSize* index_list, FlIdxSize start_index, int count) {
	int count_offset = 0;
	if (!index_list) {
		generate_recursive_count(&count_offset, start_index, count);
		return count_offset;
	}

	generate_recursive(index_list, &count_offset, start_index, start_index + count);
	return count_offset;
}

// generating triangles
// The most common way is generating this with a fan from each new vertex. This quite bad for GPUs with many long thin
// triangles.
//
// Instead we use an alternative approach which tries to minimize the area triangles are used for. Now
// there is a more accurate way to do this which is to calculate the larges availible area all the time and then
// insert a triangle there, this is quite a costly way to do it so we do something simpler given that we know
// more about what we are trying to generate (a square with potentially rounded corners)

Area* Area_generate(struct FlContext* ctx, const FlStyle* style, FlVec2 size) {
	FL_UNUSED(ctx);

	// TODO: Custom allocator
	Area* area = calloc(1, sizeof(Area));

	// TODO: Generate with border active
	if (style->border.active) {
		// TODO: Fixme
		return NULL;
	}

	// Size up with the padding
	size.x += style->padding[FlSide_Left] + style->padding[FlSide_Right];
	size.y += style->padding[FlSide_Top] + style->padding[FlSide_Bottom];

	area->content_start.x = style->padding[FlSide_Left];
	area->content_start.y = style->padding[FlSide_Top];
	area->content_end.x = style->padding[FlSide_Right];
	area->content_end.y = style->padding[FlSide_Bottom];

	u32 color = style->background_color;

	// TODO: Custom a
	area->vertex_buffer = malloc(sizeof(FlVertPosColor) * 4);
	area->index_buffer = malloc(sizeof(FlIdxSize) * 6);

	// Generate the vertex buffer with 2 triangles

	area->vertex_buffer[0].x = 0.0f;
	area->vertex_buffer[0].y = 0.0f;
	area->vertex_buffer[0].color = color;

	area->vertex_buffer[1].x = size.x;
	area->vertex_buffer[1].y = 0.0f;
	area->vertex_buffer[1].color = color;

	area->vertex_buffer[2].x = size.x;
	area->vertex_buffer[2].y = size.y;
	area->vertex_buffer[2].color = color;

	area->vertex_buffer[3].x = 0.0f;
	area->vertex_buffer[3].y = size.y;
	area->vertex_buffer[3].color = color;

	// Write index buffer

	area->index_buffer[0] = 0;
	area->index_buffer[1] = 1;
	area->index_buffer[2] = 2;

	area->index_buffer[3] = 0;
	area->index_buffer[4] = 2;
	area->index_buffer[5] = 3;

	// Counts

	area->vertex_count = 4;
	area->index_count = 6;

	return area;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Area_generate_circle(struct FlContext* ctx) {
	FlVertPosColor* vertices = NULL;
	FlIdxSize* indices = NULL;

	FlVec2* temp = alloca(400 * sizeof(FlVec2));

	FlVec2 size = { 80.0f, 80.0f };

	int count = generate_corner_values(temp, size, 40.0f, FlLengthPercentType_Length);
	int index_count = Area_generate_corner_triangle_list(NULL, 0, count - 1);

	if (!VertexAllocator_alloc_pos_color(&ctx->vertex_allocator, &vertices, &indices, count, index_count)) {
		// TODO: Error
		return;
	}

	Area_generate_corner_triangle_list(indices, 0, count - 1);

	// generate vertex list
	for (int i = 0; i < count; ++i) {
		vertices[i].x = temp[i].x * 10;
		vertices[i].y = temp[i].y * 10;
		vertices[i].color = FL_RGB(255, 0, 0);
	}

    FlRcSolidTriangles* tri_data = Render_render_flat_triangles_static(ctx->global_state, vertices, indices);
	tri_data->vertex_count = count;
	tri_data->index_count = index_count;
}





