#include <stdlib.h>
#include "area.h"
#include "style.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Areas are used for generating an area around a widget/control.
// This can be the outline for a button, box, etc.
// The look rounding/colors/etc is determined by the current style that has been set
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



// generating triangles
// The most common way is generating this with a fan from each new vertex. This quite bad for GPUs with many long thin
// triangles.
//
// Instead we use an alternative approach which tries to minimize the area triangles are used for. Now
// there is a more accurate way to do this which is to calculate the larges availible area all the time and then
// insert a triangle there, this is quite a costly way to do it so we do something simpler given that we know
// more about what we are trying to generate (a square with potentially rounded corners)

Area* Area_generate(struct FlContext* ctx, const FlStyle* style, FlVec2 size) {
	// TODO: Custom allocator
	Area* area = calloc(1, sizeof(Area));

	// TODO: Generate with border active
	if (style->border.active) {
		// TODO: Fixme
		return NULL;
	}

	// Size up with the padding
	size.x += style->padding.sides[FlSide_Left] + style->padding.sides[FlSide_Right];
	size.y += style->padding.sides[FlSide_Top] + style->padding.sides[FlSide_Bottom];

	area->content_start.x = style->padding.sides[FlSide_Left];
	area->content_start.y = style->padding.sides[FlSide_Top];
	area->content_end.x = style->padding.sides[FlSide_Right];
	area->content_end.y = style->padding.sides[FlSide_Bottom];

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
