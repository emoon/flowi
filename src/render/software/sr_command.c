#include "sr_command.h"
#include <core/core.h>
#include <core/arena.h>
#include <core/log.h>
#include <core/simd.h>
#include "sr_gradient_interpolation.h"
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pre-compute load_offset and shuffle_mask for every possible starting position, so tiles can start at any
// position without runtime computation.

static ScaledTexturePrecomputed* calculate_shuffle_tables(FlArena* arena, float src_width, int dst_width) {
    FL_VALIDATE_RET(dst_width > 0, nullptr);

    float x_step = src_width / (float)dst_width;

    // One entry per starting position, plus 16 entries of padding so 8-wide reads from any start stay in bounds.
    size_t array_size = (size_t)dst_width + 16;
    ScaledTexturePrecomputed* result = arena_alloc_zero(arena, ScaledTexturePrecomputed);
    result->load_offsets = arena_alloc_array(arena, u16, array_size);
    result->shuffle_masks = arena_alloc_array(arena, u8x16, array_size);
    result->dst_width = dst_width;

    uint32_t max_src_pixel = ((uint32_t)src_width > 0) ? ((uint32_t)src_width - 1) : 0;

    // Source column per destination pixel; also drives the scalar-gather path for wide spreads.
    u16* all_src_indices = arena_alloc_array(arena, u16, array_size);
    result->src_indices = all_src_indices;
    for (int dx = 0; dx < dst_width; dx++) {
        float src_f = (float)dx * x_step;
        uint32_t src_idx = (uint32_t)floorf(src_f);
        if (src_idx > max_src_pixel) {
            src_idx = max_src_pixel;
        }
        all_src_indices[dx] = (u16)src_idx;
    }
    for (size_t dx = (size_t)dst_width; dx < array_size; dx++) {
        all_src_indices[dx] = (u16)max_src_pixel;
    }

    // Widest source-index spread across any 8-pixel group. The shuffle window covers 32 bytes (indices 0..31).
    u16 max_spread = 0;
    for (int start = 0; start < dst_width; start++) {
        u16x8 src_indices = u16x8_load_unaligned(all_src_indices, start);

        u16 min_idx = u16x8_hmin(src_indices);
        result->load_offsets[start] = min_idx;

        // Source indices are monotonically non-decreasing in dx, so the last of the 8 is the max;
        // its distance from the min is the group's byte spread.
        u16 spread = (u16)(all_src_indices[start + 7] - min_idx);
        if (spread > max_spread) {
            max_spread = spread;
        }

        u16x8 min_splat = u16x8_new_splat(min_idx);
        u16x8 offsets = u16x8_sub(src_indices, min_splat);

        // The upper 8 bytes of the mask come out zero from the zero vector
        u16x8 zero = u16x8_new_splat(0);
        result->shuffle_masks[start] = u16x8_narrow_to_u8x16(offsets, zero);
    }

    // One group with a spread beyond the window switches every group in this command to the scalar gather.
    result->wide_spread = (max_spread > 31);

    for (size_t start = (size_t)dst_width; start < array_size; start++) {
        result->load_offsets[start] = result->load_offsets[dst_width - 1];
        result->shuffle_masks[start] = result->shuffle_masks[dst_width - 1];
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sr_command_setup(FlArena* arena, RenderCommand* command) {
    if (command->mask & RenderMask_ColorGradient) {
        i32x4 rect_int = f32x4_as_i32x4(command->aabb);
        GradientQuadState* grad_state = arena_alloc(arena, GradientQuadState);
        gradient_interpolation_setup(grad_state, rect_int, command->corner_colors);
        command->gradient_data = grad_state;
    }

    if (command->mask & RenderMask_TextureScaled) {
        float dst_width = f32x4_extract(command->aabb, 2) - f32x4_extract(command->aabb, 0);
        // Unpadded source width: the scale factor is source pixels per destination pixel.
        float src_width = (float)command->image->width;

        command->scaled_texture = calculate_shuffle_tables(arena, src_width, (int)dst_width);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CommandBuilder {
    RenderCommand* commands;
    RenderCommand* initial_command;
} CommandBuilder;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static CommandBuilder builder_create(FlArena* arena, RenderCommand* initial_command, int command_count) {
    return (CommandBuilder) {
        .commands = arena_alloc_array(arena, RenderCommand, command_count),
        .initial_command = initial_command,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void add_corner(CommandBuilder* builder, f32x4 aabb, float radius, RenderCorner corner) {
    RenderCommand* current = builder->commands++;
    *current = *builder->initial_command;

    f32x4 clip_adjust = f32x4_sub(aabb, builder->initial_command->aabb);
    i16 x_adjust = (i16)f32x4_extract(clip_adjust, 0);
    i16 y_adjust = (i16)f32x4_extract(clip_adjust, 1);

    current->aabb = aabb;
    current->clip_adjust = i16x8_new(x_adjust, y_adjust, 0, 0, 0, 0, 0, 0);
    current->radius = radius;
    current->mask |= RenderMask_Corners | render_mask_corner(corner);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void add_rect(CommandBuilder* builder, f32x4 aabb) {
    RenderCommand* current = builder->commands++;
    *current = *builder->initial_command;

    f32x4 clip_adjust = f32x4_sub(aabb, builder->initial_command->aabb);
    i16 x_adjust = (i16)f32x4_extract(clip_adjust, 0);
    i16 y_adjust = (i16)f32x4_extract(clip_adjust, 1);

    current->clip_adjust = i16x8_new(x_adjust, y_adjust, 0, 0, 0, 0, 0, 0);
    current->aabb = aabb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void add_corner_rect(CommandBuilder* builder, float x, float y, float radius, RenderCorner corner) {
    add_corner(builder, f32x4_new(x, y, x + radius, y + radius), radius, corner);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void add_fill_rect(CommandBuilder* builder, float x, float y, float w, float h) {
    add_rect(builder, f32x4_new(x, y, x + w, y + h));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_rounding(float tl, float tr, float bl, float br) {
    return (tl > 0.0f || tr > 0.0f || bl > 0.0f || br > 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_uniform_corners(float tl, float tr, float bl, float br) {
    return (tl > 0.0f && tl == tr && tr == bl && bl == br);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_only_top_corners(float tl, float tr, float bl, float br) {
    return (tl > 0.0f && tr > 0.0f && bl == 0.0f && br == 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_only_bottom_corners(float tl, float tr, float bl, float br) {
    return (tl == 0.0f && tr == 0.0f && bl > 0.0f && br > 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_only_left_corners(float tl, float tr, float bl, float br) {
    return (tl > 0.0f && tr == 0.0f && bl > 0.0f && br == 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_only_right_corners(float tl, float tr, float bl, float br) {
    return (tl == 0.0f && tr > 0.0f && bl == 0.0f && br > 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int sr_command_split_rounded(FlArena* arena, RenderCommand* initial_command, f32x4 corners) {
    float tl = f32x4_extract(corners, 0);
    float tr = f32x4_extract(corners, 1);
    float bl = f32x4_extract(corners, 2);
    float br = f32x4_extract(corners, 3);

    float x = f32x4_extract(initial_command->aabb, 0);
    float y = f32x4_extract(initial_command->aabb, 1);
    float width = f32x4_extract(initial_command->aabb, 2) - x;
    float height = f32x4_extract(initial_command->aabb, 3) - y;

    if (!has_rounding(tl, tr, bl, br)) {
        RenderCommand* commands = arena_alloc(arena, RenderCommand);
        *commands = *initial_command;
        return 1;
    }

    // Uniform corners - optimized 7-rect layout
    if (FL_LIKELY(has_uniform_corners(tl, tr, bl, br))) {
        CommandBuilder builder = builder_create(arena, initial_command, 7);
        float radius = tl;

        add_corner_rect(&builder, x, y, radius, RenderCorner_TopLeft);
        add_corner_rect(&builder, x + width - radius, y, radius, RenderCorner_TopRight);
        add_corner_rect(&builder, x, y + height - radius, radius, RenderCorner_BottomLeft);
        add_corner_rect(&builder, x + width - radius, y + height - radius, radius, RenderCorner_BottomRight);

        add_fill_rect(&builder, x + radius, y, (width - 2 * radius), radius);                 // top strip
        add_fill_rect(&builder, x + radius, y + height - radius, width - 2 * radius, radius); // bottom strip
        add_fill_rect(&builder, x, y + radius, width, height - 2 * radius);                   // center

        return 7;
    }

    if (has_only_top_corners(tl, tr, bl, br)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        const float max_radius = fmaxf(tl, tr);

        add_corner_rect(&builder, x, y, tl, RenderCorner_TopLeft);
        add_corner_rect(&builder, x + width - tr, y, tr, RenderCorner_TopRight);
        add_fill_rect(&builder, x + tl, y, width - tl - tr, max_radius);
        add_fill_rect(&builder, x, y + max_radius, width, height - max_radius);
        return 4;
    }

    if (has_only_bottom_corners(tl, tr, bl, br)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        const float max_radius = fmaxf(bl, br);

        add_corner_rect(&builder, x, y + height - bl, bl, RenderCorner_BottomLeft);
        add_corner_rect(&builder, x + width - br, y + height - br, br, RenderCorner_BottomRight);
        add_fill_rect(&builder, x + bl, y + height - max_radius, width - bl - br, max_radius);
        add_fill_rect(&builder, x, y, width, height - max_radius);
        return 4;
    }

    if (has_only_left_corners(tl, tr, bl, br)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        const float max_radius = fmaxf(tl, bl);

        add_corner_rect(&builder, x, y, tl, RenderCorner_TopLeft);
        add_corner_rect(&builder, x, y + height - bl, bl, RenderCorner_BottomLeft);
        add_fill_rect(&builder, x, y + tl, max_radius, height - tl - bl);
        add_fill_rect(&builder, x + max_radius, y, width - max_radius, height);
        return 4;
    }

    if (has_only_right_corners(tl, tr, bl, br)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        const float max_radius = fmaxf(tr, br);

        add_corner_rect(&builder, x + width - tr, y, tr, RenderCorner_TopRight);
        add_corner_rect(&builder, x + width - br, y + height - br, br, RenderCorner_BottomRight);
        add_fill_rect(&builder, x + width - max_radius, y + tr, max_radius, height - tr - br);
        add_fill_rect(&builder, x, y, width - max_radius, height);
        return 4;
    }

    // General case - 9-grid layout for mixed corner patterns

    // Only non-zero corners emit a corner command, so size the allocation to exactly the commands we write
    // (corners + 5 fills). Over-allocating leaves uninitialized slots that the caller's arena_count would walk.
    const int corner_count = (tl > 0.0f) + (tr > 0.0f) + (bl > 0.0f) + (br > 0.0f);
    const int command_count = corner_count + 5;
    CommandBuilder builder = builder_create(arena, initial_command, command_count);

    const float left_width = fmaxf(tl, bl);
    const float right_width = fmaxf(tr, br);
    const float center_width = width - left_width - right_width;

    const float top_height = fmaxf(tl, tr);
    const float bottom_height = fmaxf(bl, br);
    const float center_height = height - top_height - bottom_height;

    if (tl > 0.0f)
        add_corner_rect(&builder, x, y, tl, RenderCorner_TopLeft);
    if (tr > 0.0f)
        add_corner_rect(&builder, x + width - tr, y, tr, RenderCorner_TopRight);
    if (bl > 0.0f)
        add_corner_rect(&builder, x, y + height - bl, bl, RenderCorner_BottomLeft);
    if (br > 0.0f)
        add_corner_rect(&builder, x + width - br, y + height - br, br, RenderCorner_BottomRight);

    add_fill_rect(&builder, x + left_width, y, center_width, top_height); // top center
    add_fill_rect(&builder, x + left_width, y + height - bottom_height, center_width,
                  bottom_height);                                                                 // bottom center
    add_fill_rect(&builder, x, y + top_height, left_width, center_height);                        // left center
    add_fill_rect(&builder, x + width - right_width, y + top_height, right_width, center_height); // right center
    add_fill_rect(&builder, x + left_width, y + top_height, center_width, center_height);         // center

    return command_count;
}
