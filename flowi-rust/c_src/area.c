#include "area.h"
#include <assert.h>
#include <flowi/style.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "internal.h"
//#include "render.h"

#if 0

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Areas are used for generating an area around a widget/control.
// This can be the outline for a button, box, etc.
// The look rounding/colors/etc is determined by the current style that has been set
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// generating triangles
// The most common way is generating this with a fan from each new vertex. This quite bad for GPUs with many long thin
// triangles.
//
// Instead we use an alternative approach which tries to minimize the area triangles are used for. Now
// there is a more accurate way to do this which is to calculate the larges availible area all the time and then
// insert a triangle there, this is quite a costly way to do it so we do something simpler given that we know
// more about what we are trying to generate (a square with potentially rounded corners)

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
    generate_recursive(index_list, &count_offset, start_index, start_index + count);
    return count_offset;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE int corner_triangle_list_calc(int count) {
    return (count - 1) * 3;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CornerVerts {
    FlIdxSize idx[8];
    int count;
} CornerVerts;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void corner_add_idx(CornerVerts* verts, FlIdxSize idx) {
    verts->idx[verts->count++] = idx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int generate_corner(struct FlContext* ctx, float side, FlVec2 size, CornerVerts* corner_verts,
                           const FlStyle* style, FlVec2 offset, int vertex_offset, int corner_index) {
    FlIdxSize* indices = NULL;
    FlVertPosColor* cverts = NULL;

    // const int segment_size = 2;

    const FlLengthPercentValue border_radius = style->border.radius[corner_index];
    const float pixel_side = border_radius.value;
    const int corner_pixels_percent = (int)(side * (border_radius.value * 0.01));
    const int corner_pixels = border_radius.typ == FlLengthPercent_Length ? (int)pixel_side : corner_pixels_percent;
    int vertex_count = corner_pixels;

    // with lots of vertices reducing to half is fine
    if (vertex_count >= 64) {
        vertex_count /= 2;
    }

    if (vertex_count == 0) {
        return 0;
    }

    int index_count = corner_triangle_list_calc(vertex_count - 1);

    VertexAllocator_alloc_pos_color(&ctx->vertex_allocator, &cverts, &indices, vertex_count, index_count);

    Area_generate_corner_triangle_list(indices, vertex_offset, vertex_count - 1);

    const float sin_step = (M_PI / 2.0f) / vertex_count;
    float angle = 0.0f;

    FL_ASSUME(vertex_count > 0);

    // TODO: do this better

    switch (corner_index) {
        case FlCorner_TopLeft: {
            // float start_x = 0.0f;
            for (int i = 0; i < vertex_count; ++i) {
                cverts[i].x = offset.x + (corner_pixels - ((float)(cos(angle) * corner_pixels)));
                cverts[i].y = offset.y + (corner_pixels - ((float)(sin(angle) * corner_pixels)));
                cverts[i].color = style->background_color;
                angle += sin_step;
            }

            corner_add_idx(corner_verts, vertex_offset);
            corner_add_idx(corner_verts, vertex_offset + (vertex_count - 1));

            break;
        }

        case FlCorner_TopRight: {
            float start_x = size.x - corner_pixels;
            for (int i = 0; i < vertex_count; ++i) {
                cverts[i].x = offset.x + start_x + ((float)(cos(angle) * corner_pixels));
                cverts[i].y = offset.y + (corner_pixels - ((float)(sin(angle) * corner_pixels)));
                cverts[i].color = style->background_color;
                angle += sin_step;
            }

            corner_add_idx(corner_verts, vertex_offset + (vertex_count - 1));
            corner_add_idx(corner_verts, vertex_offset);

            break;
        }

        case FlCorner_BottomRight: {
            float start_x = size.x - corner_pixels;
            float start_y = size.y - corner_pixels;
            for (int i = 0; i < vertex_count; ++i) {
                cverts[i].x = offset.x + start_x + (float)(cos(angle) * corner_pixels);
                cverts[i].y = offset.y + start_y + (float)(sin(angle) * corner_pixels);
                cverts[i].color = style->background_color;
                angle += sin_step;
            }

            corner_add_idx(corner_verts, vertex_offset);
            corner_add_idx(corner_verts, vertex_offset + (vertex_count - 1));

            break;
        }

        case FlCorner_BottomLeft: {
            float start_y = size.y - corner_pixels;
            for (int i = 0; i < vertex_count; ++i) {
                cverts[i].x = offset.x + (corner_pixels - (float)(cos(angle) * corner_pixels));
                cverts[i].y = offset.y + start_y + ((float)(sin(angle) * corner_pixels));
                cverts[i].color = style->background_color;
                angle += sin_step;
            }

            corner_add_idx(corner_verts, vertex_offset + (vertex_count - 1));
            corner_add_idx(corner_verts, vertex_offset);

            break;
        }
    }

    return vertex_count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: This isn't really generating optimial triangle layout (to minimize area usage so we need to fix that)

void join_corners(struct FlContext* ctx, CornerVerts* corners) {
    FlVertPosColor* vertices = NULL;
    FlIdxSize* indices = NULL;

    const int triangle_count = (corners->count - 2);
    VertexAllocator_alloc_pos_color(&ctx->vertex_allocator, &vertices, &indices, 0, triangle_count * 3);

    for (int i = 0; i < triangle_count; ++i) {
        *indices++ = corners->idx[0];
        *indices++ = corners->idx[i + 1];
        *indices++ = corners->idx[i + 2];
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool generate_corners(struct FlContext* ctx, const FlStyle* style, FlVec2 offset, FlVec2 size) {
    CornerVerts corners[4] = {0};

    // TODO: This should be passed in
    int index = 0;

    const float half_x = size.x * 0.5f;
    const float half_y = size.y * 0.5f;
    const float shortest_side = half_x > half_y ? half_y : half_x;
    const u32 background_color = style->background_color;

    FlVec2 area_corners[4] = {
        {0.0f, 0.0f},
        {size.x, 0.0f},
        {size.x, size.y},
        {0.0f, size.y},
    };

    for (int i = 0; i < 4; ++i) {
        int vertex_count = generate_corner(ctx, shortest_side, size, corners, style, offset, index, i);
        if (vertex_count == 0) {
            FlVertPosColor* vertices = NULL;
            FlIdxSize* indices = NULL;

            VertexAllocator_alloc_pos_color(&ctx->vertex_allocator, &vertices, &indices, 1, 0);

            vertices[0].x = offset.x + area_corners[i].x;
            vertices[0].y = offset.y + area_corners[i].y;
            vertices[0].color = background_color;
            corner_add_idx(corners, index);

            vertex_count = 1;
        }

        index += vertex_count;
    }

    join_corners(ctx, corners);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate area

/*
void Area_generate(struct FlContext* ctx, const FlStyle* style, FlVec2 pos, FlVec2 size) {
    FlStyle* style = fl_style_get_default(ctx);

    generate_corners(ctx, style, pos, size);

    VertsCounts counts = VertexAllocator_get_pos_color_counts(&ctx->vertex_allocator);
    FlSolidTriangles* tri_data = Render_solid_triangles_cmd(ctx->global);

    tri_data->offset = ctx->vertex_allocator.frame_index;
    tri_data->vertex_buffer = counts.vertex_data;
    tri_data->index_buffer = counts.index_data;

    tri_data->vertex_buffer_size = counts.vertex_count;
    tri_data->index_buffer_size = counts.index_count;
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Area_generate_circle(struct FlContext* ctx) {
    FlStyle* style = fl_style_get_default(ctx);

    FlVec2 offset = {10.0f, 10.f};
    FlVec2 size = {280.0f * 2, 280 * 2.0f};

    style->background_color = FL_RGB(0x19, 0xe, 0x23);

    style->border.radius[0].value = 40.0f;
    style->border.radius[1].value = 40.0f;
    style->border.radius[2].value = 40.0f;
    style->border.radius[3].value = 40.0f;

    generate_corners(ctx, style, offset, size);

    VertsCounts counts = VertexAllocator_get_pos_color_counts(&ctx->vertex_allocator);
    FlSolidTriangles* tri_data = Render_solid_triangles_cmd(ctx->global);

    tri_data->offset = ctx->vertex_allocator.frame_index;
    tri_data->vertex_buffer = counts.vertex_data;
    tri_data->index_buffer = counts.index_data;
    // TODO: Use bytes here?
    tri_data->vertex_buffer_size = counts.vertex_count;
    tri_data->index_buffer_size = counts.index_count;

    return true;
}

#endif
