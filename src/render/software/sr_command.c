#include "sr_command.h"
#include <core/core.h>
#include <core/arena.h>
#include <core/log.h>
#include <core/simd.h>
#include "sr_gradient_interpolation.h"
#include <math.h>

// Pre-compute load_offset and shuffle_mask for every possible starting position, so tiles can start at any
// position without runtime computation.

// Source column per destination pixel; also drives the scalar-gather path for wide spreads.
static void fill_src_indices(u16* out, float src_width, int dst_width, size_t array_size) {
    const float x_step = src_width / (float)dst_width;
    const uint32_t max_src_pixel = ((uint32_t)src_width > 0) ? ((uint32_t)src_width - 1) : 0;

    for (int dx = 0; dx < dst_width; dx++) {
        uint32_t src_idx = (uint32_t)floorf((float)dx * x_step);
        out[dx] = (u16)(src_idx > max_src_pixel ? max_src_pixel : src_idx);
    }
    for (size_t dx = (size_t)dst_width; dx < array_size; dx++) {
        out[dx] = (u16)max_src_pixel;
    }
}

// Fills load_offsets and shuffle_masks for every start position and returns the widest source-index spread across
// any 8-pixel group. The shuffle window covers 32 bytes (indices 0..31).
static u16 fill_shuffle_entries(ScaledTexturePrecomputed* tables, const u16* src_indices, int dst_width) {
    u16 max_spread = 0;
    for (int start = 0; start < dst_width; start++) {
        u16x8 indices = u16x8_load_unaligned(src_indices, start);

        u16 min_idx = u16x8_hmin(indices);
        tables->load_offsets[start] = min_idx;

        // Source indices are monotonically non-decreasing in dx, so the last of the 8 is the max;
        // its distance from the min is the group's byte spread.
        u16 spread = (u16)(src_indices[start + 7] - min_idx);
        max_spread = (spread > max_spread) ? spread : max_spread;

        u16x8 offsets = u16x8_sub(indices, u16x8_new_splat(min_idx));

        // The upper 8 bytes of the mask come out zero from the zero vector
        tables->shuffle_masks[start] = u16x8_narrow_to_u8x16(offsets, u16x8_new_splat(0));
    }
    return max_spread;
}

static ScaledTexturePrecomputed* calculate_shuffle_tables(FlArena* arena, float src_width, int dst_width) {
    FL_VALIDATE_RET(dst_width > 0, nullptr);

    // One entry per starting position, plus 16 entries of padding so 8-wide reads from any start stay in bounds.
    size_t array_size = (size_t)dst_width + 16;
    ScaledTexturePrecomputed* result = arena_alloc_zero(arena, ScaledTexturePrecomputed);
    result->load_offsets = arena_alloc_array(arena, u16, array_size);
    result->shuffle_masks = arena_alloc_array(arena, u8x16, array_size);
    result->dst_width = dst_width;

    u16* all_src_indices = arena_alloc_array(arena, u16, array_size);
    result->src_indices = all_src_indices;
    fill_src_indices(all_src_indices, src_width, dst_width, array_size);

    // One group with a spread beyond the window switches every group in this command to the scalar gather.
    result->wide_spread = (fill_shuffle_entries(result, all_src_indices, dst_width) > 31);

    for (size_t start = (size_t)dst_width; start < array_size; start++) {
        result->load_offsets[start] = result->load_offsets[dst_width - 1];
        result->shuffle_masks[start] = result->shuffle_masks[dst_width - 1];
    }

    return result;
}

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

typedef struct CommandBuilder {
    RenderCommand* commands;
    RenderCommand* initial_command;
} CommandBuilder;

static CommandBuilder builder_create(FlArena* arena, RenderCommand* initial_command, int command_count) {
    return (CommandBuilder) {
        .commands = arena_alloc_array(arena, RenderCommand, command_count),
        .initial_command = initial_command,
    };
}

typedef struct Rect {
    float x;
    float y;
    float w;
    float h;
} Rect;

typedef struct CornerRect {
    float x;
    float y;
    float radius;
    RenderCorner corner;
} CornerRect;

typedef struct Corners {
    float tl;
    float tr;
    float bl;
    float br;
} Corners;

static RenderCommand* add_command(CommandBuilder* builder, f32x4 aabb) {
    RenderCommand* current = builder->commands++;
    *current = *builder->initial_command;

    f32x4 clip_adjust = f32x4_sub(aabb, builder->initial_command->aabb);
    i16 x_adjust = (i16)f32x4_extract(clip_adjust, 0);
    i16 y_adjust = (i16)f32x4_extract(clip_adjust, 1);

    current->aabb = aabb;
    current->clip_adjust = i16x8_new(x_adjust, y_adjust, 0, 0, 0, 0, 0, 0);
    return current;
}

static void add_corner_rect(CommandBuilder* builder, CornerRect c) {
    RenderCommand* current = add_command(builder, f32x4_new(c.x, c.y, c.x + c.radius, c.y + c.radius));
    current->radius = c.radius;
    current->mask |= RenderMask_Corners | render_mask_corner(c.corner);
}

static void add_fill_rect(CommandBuilder* builder, Rect r) {
    add_command(builder, f32x4_new(r.x, r.y, r.x + r.w, r.y + r.h));
}

static bool has_rounding(const Corners* c) {
    return (c->tl > 0.0f || c->tr > 0.0f || c->bl > 0.0f || c->br > 0.0f);
}

static bool has_uniform_corners(const Corners* c) {
    return (c->tl > 0.0f && c->tl == c->tr && c->tr == c->bl && c->bl == c->br);
}

static bool has_only_top_corners(const Corners* c) {
    return (c->tl > 0.0f && c->tr > 0.0f && c->bl == 0.0f && c->br == 0.0f);
}

static bool has_only_bottom_corners(const Corners* c) {
    return (c->tl == 0.0f && c->tr == 0.0f && c->bl > 0.0f && c->br > 0.0f);
}

static bool has_only_left_corners(const Corners* c) {
    return (c->tl > 0.0f && c->tr == 0.0f && c->bl > 0.0f && c->br == 0.0f);
}

static bool has_only_right_corners(const Corners* c) {
    return (c->tl == 0.0f && c->tr > 0.0f && c->bl == 0.0f && c->br > 0.0f);
}

// Uniform corners: 7-rect layout
static int split_uniform(CommandBuilder* builder, const Rect* r, float radius) {
    const float x = r->x, y = r->y, width = r->w, height = r->h;

    add_corner_rect(builder, (CornerRect) { x, y, radius, RenderCorner_TopLeft });
    add_corner_rect(builder, (CornerRect) { x + width - radius, y, radius, RenderCorner_TopRight });
    add_corner_rect(builder, (CornerRect) { x, y + height - radius, radius, RenderCorner_BottomLeft });
    add_corner_rect(builder,
                    (CornerRect) { x + width - radius, y + height - radius, radius, RenderCorner_BottomRight });

    add_fill_rect(builder, (Rect) { x + radius, y, (width - 2 * radius), radius });                 // top strip
    add_fill_rect(builder, (Rect) { x + radius, y + height - radius, width - 2 * radius, radius }); // bottom strip
    add_fill_rect(builder, (Rect) { x, y + radius, width, height - 2 * radius });                   // center

    return 7;
}

static int split_top(CommandBuilder* builder, const Rect* r, const Corners* c) {
    const float max_radius = fmaxf(c->tl, c->tr);

    add_corner_rect(builder, (CornerRect) { r->x, r->y, c->tl, RenderCorner_TopLeft });
    add_corner_rect(builder, (CornerRect) { r->x + r->w - c->tr, r->y, c->tr, RenderCorner_TopRight });
    add_fill_rect(builder, (Rect) { r->x + c->tl, r->y, r->w - c->tl - c->tr, max_radius });
    add_fill_rect(builder, (Rect) { r->x, r->y + max_radius, r->w, r->h - max_radius });
    return 4;
}

static int split_bottom(CommandBuilder* builder, const Rect* r, const Corners* c) {
    const float max_radius = fmaxf(c->bl, c->br);

    add_corner_rect(builder, (CornerRect) { r->x, r->y + r->h - c->bl, c->bl, RenderCorner_BottomLeft });
    add_corner_rect(builder,
                    (CornerRect) { r->x + r->w - c->br, r->y + r->h - c->br, c->br, RenderCorner_BottomRight });
    add_fill_rect(builder, (Rect) { r->x + c->bl, r->y + r->h - max_radius, r->w - c->bl - c->br, max_radius });
    add_fill_rect(builder, (Rect) { r->x, r->y, r->w, r->h - max_radius });
    return 4;
}

static int split_left(CommandBuilder* builder, const Rect* r, const Corners* c) {
    const float max_radius = fmaxf(c->tl, c->bl);

    add_corner_rect(builder, (CornerRect) { r->x, r->y, c->tl, RenderCorner_TopLeft });
    add_corner_rect(builder, (CornerRect) { r->x, r->y + r->h - c->bl, c->bl, RenderCorner_BottomLeft });
    add_fill_rect(builder, (Rect) { r->x, r->y + c->tl, max_radius, r->h - c->tl - c->bl });
    add_fill_rect(builder, (Rect) { r->x + max_radius, r->y, r->w - max_radius, r->h });
    return 4;
}

static int split_right(CommandBuilder* builder, const Rect* r, const Corners* c) {
    const float max_radius = fmaxf(c->tr, c->br);

    add_corner_rect(builder, (CornerRect) { r->x + r->w - c->tr, r->y, c->tr, RenderCorner_TopRight });
    add_corner_rect(builder,
                    (CornerRect) { r->x + r->w - c->br, r->y + r->h - c->br, c->br, RenderCorner_BottomRight });
    add_fill_rect(builder, (Rect) { r->x + r->w - max_radius, r->y + c->tr, max_radius, r->h - c->tr - c->br });
    add_fill_rect(builder, (Rect) { r->x, r->y, r->w - max_radius, r->h });
    return 4;
}

// Only non-zero corners emit a corner command.
static void add_present_corners(CommandBuilder* builder, const Rect* r, const Corners* c) {
    if (c->tl > 0.0f)
        add_corner_rect(builder, (CornerRect) { r->x, r->y, c->tl, RenderCorner_TopLeft });
    if (c->tr > 0.0f)
        add_corner_rect(builder, (CornerRect) { r->x + r->w - c->tr, r->y, c->tr, RenderCorner_TopRight });
    if (c->bl > 0.0f)
        add_corner_rect(builder, (CornerRect) { r->x, r->y + r->h - c->bl, c->bl, RenderCorner_BottomLeft });
    if (c->br > 0.0f)
        add_corner_rect(builder,
                        (CornerRect) { r->x + r->w - c->br, r->y + r->h - c->br, c->br, RenderCorner_BottomRight });
}

// General case: 9-grid layout for mixed corner patterns
static void add_grid_fills(CommandBuilder* builder, const Rect* r, const Corners* c) {
    const float left_width = fmaxf(c->tl, c->bl);
    const float right_width = fmaxf(c->tr, c->br);
    const float center_width = r->w - left_width - right_width;

    const float top_height = fmaxf(c->tl, c->tr);
    const float bottom_height = fmaxf(c->bl, c->br);
    const float center_height = r->h - top_height - bottom_height;

    add_fill_rect(builder, (Rect) { r->x + left_width, r->y, center_width, top_height }); // top center
    add_fill_rect(builder, (Rect) { r->x + left_width, r->y + r->h - bottom_height, center_width,
                                    bottom_height });                                      // bottom center
    add_fill_rect(builder, (Rect) { r->x, r->y + top_height, left_width, center_height }); // left center
    add_fill_rect(builder,
                  (Rect) { r->x + r->w - right_width, r->y + top_height, right_width, center_height }); // right center
    add_fill_rect(builder, (Rect) { r->x + left_width, r->y + top_height, center_width, center_height }); // center
}

static int split_command_count(const Corners* c) {
    // Corners + 5 fills. Over-allocating leaves uninitialized slots that the caller's arena_count would walk.
    return (c->tl > 0.0f) + (c->tr > 0.0f) + (c->bl > 0.0f) + (c->br > 0.0f) + 5;
}

int sr_command_split_rounded(FlArena* arena, RenderCommand* initial_command, f32x4 corners) {
    const Corners c = {
        f32x4_extract(corners, 0),
        f32x4_extract(corners, 1),
        f32x4_extract(corners, 2),
        f32x4_extract(corners, 3),
    };

    const f32x4 aabb = initial_command->aabb;
    const float x = f32x4_extract(aabb, 0);
    const float y = f32x4_extract(aabb, 1);
    const Rect rect = { x, y, f32x4_extract(aabb, 2) - x, f32x4_extract(aabb, 3) - y };

    if (!has_rounding(&c)) {
        RenderCommand* commands = arena_alloc(arena, RenderCommand);
        *commands = *initial_command;
        return 1;
    }

    if (FL_LIKELY(has_uniform_corners(&c))) {
        CommandBuilder builder = builder_create(arena, initial_command, 7);
        return split_uniform(&builder, &rect, c.tl);
    }

    if (has_only_top_corners(&c)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        return split_top(&builder, &rect, &c);
    }

    if (has_only_bottom_corners(&c)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        return split_bottom(&builder, &rect, &c);
    }

    if (has_only_left_corners(&c)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        return split_left(&builder, &rect, &c);
    }

    if (has_only_right_corners(&c)) {
        CommandBuilder builder = builder_create(arena, initial_command, 4);
        return split_right(&builder, &rect, &c);
    }

    const int command_count = split_command_count(&c);
    CommandBuilder builder = builder_create(arena, initial_command, command_count);
    add_present_corners(&builder, &rect, &c);
    add_grid_fills(&builder, &rect, &c);
    return command_count;
}
