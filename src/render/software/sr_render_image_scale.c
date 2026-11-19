#include "sr_render_image_scale.h"
#include <core/log.h>
#include <core/simd.h>
#include "software_renderer_internal.h"
#include "sr_image_blend.h"
#include "sr_tile_row.h"
#include <ui/image/image.h>
#include <math.h>

// Sample 8 destination pixels from one source plane as Q7 values. Takes the scalar gather when this command's
// source spread does not fit the 32-byte shuffle window.
static ALWAYS_INLINE i16x8 sample_plane_8(const ScaledTexturePrecomputed* scaled, const u8* line, int dest_x_pos) {
    u8x16 bytes;
    if (scaled->wide_spread) {
        // Indices are clamped to the valid source column range at precompute time.
        u8 gathered[16] = { 0 };
        const u16* src_indices = scaled->src_indices + dest_x_pos;
        for_count(i, 8) {
            gathered[i] = line[src_indices[i]];
        }
        bytes = u8x16_load_unaligned(gathered, 0);
    } else {
        const size_t load_offset = scaled->load_offsets[dest_x_pos];
        bytes = u8x16_shuffle2(u8x16_load_unaligned(line, load_offset), u8x16_load_unaligned(line, load_offset + 16),
                               scaled->shuffle_masks[dest_x_pos]);
    }
    return u8x16_unpack_to_i16x8_wide_shl(bytes, 7).v0;
}

typedef struct ScaleState {
    const ScaledTexturePrecomputed* scaled;
    SoaRgba tint;
    i16x8 global_alpha;
    int channel_count;
    bool fade;
} ScaleState;

// One source row: a pointer per plane. Planes past channel_count are never dereferenced.
typedef struct ScaleSourceRow {
    const u8* plane_lines[4];
} ScaleSourceRow;

static ALWAYS_INLINE i16x8 sample_row_plane(const ScaleState* state, const ScaleSourceRow* src, int plane,
                                            int dest_x_pos) {
    return sample_plane_8(state->scaled, src->plane_lines[plane], dest_x_pos);
}

// Alpha coverage for the command tint.
static ALWAYS_INLINE SoaRgb scale_1ch(const ScaleState* state, const ScaleSourceRow* src, int dest_x_pos,
                                      const PlaneRow* row) {
    i16x8 coverage = sample_row_plane(state, src, 0, dest_x_pos);
    coverage = i16x8_lerp(i16x8_new_splat(0), coverage, state->global_alpha);
    SoaRgb tint = { .r = state->tint.r, .g = state->tint.g, .b = state->tint.b };
    return lerp_over_background(tint, row, coverage);
}

static ALWAYS_INLINE SoaRgb sample_rgb(const ScaleState* state, const ScaleSourceRow* src, int dest_x_pos) {
    return (SoaRgb) {
        .r = sample_row_plane(state, src, 0, dest_x_pos),
        .g = sample_row_plane(state, src, 1, dest_x_pos),
        .b = sample_row_plane(state, src, 2, dest_x_pos),
    };
}

// Premultiplied RGBA.
static ALWAYS_INLINE SoaRgb scale_4ch(const ScaleState* state, const ScaleSourceRow* src, int dest_x_pos,
                                      const PlaneRow* row) {
    const SoaRgb rgb = sample_rgb(state, src, dest_x_pos);
    SoaRgba texel = {
        .r = rgb.r,
        .g = rgb.g,
        .b = rgb.b,
        .a = i16x8_lerp(i16x8_new_splat(0), sample_row_plane(state, src, 3, dest_x_pos), state->global_alpha),
    };
    return premul_over_8(texel, state->global_alpha, plane_load_rgb(row));
}

// Opaque unless faded by the frame's global alpha.
static ALWAYS_INLINE SoaRgb scale_3ch(const ScaleState* state, const ScaleSourceRow* src, int dest_x_pos,
                                      const PlaneRow* row) {
    SoaRgb color = sample_rgb(state, src, dest_x_pos);

    if (state->fade) {
        color = lerp_over_background(color, row, state->global_alpha);
    }
    return color;
}

static ALWAYS_INLINE SoaRgb scale_8_pixels(const ScaleState* state, const ScaleSourceRow* src, int dest_x_pos,
                                           const PlaneRow* row) {
    if (state->channel_count == 1) {
        return scale_1ch(state, src, dest_x_pos, row);
    }

    if (state->channel_count == 4) {
        return scale_4ch(state, src, dest_x_pos, row);
    }

    return scale_3ch(state, src, dest_x_pos, row);
}

static bool channel_count_is_supported(int channel_count) {
    return channel_count == 1 || channel_count == 3 || channel_count == 4;
}

static bool scaled_texture_is_ready(const ScaledTexturePrecomputed* scaled) {
    return scaled && scaled->load_offsets && scaled->shuffle_masks;
}

static bool scale_command_is_valid(const RenderCommand* command) {
    if (!channel_count_is_supported(command->image->channels)) {
        log_error("sr_render_image_scale: unsupported channel count %d", command->image->channels);
        return false;
    }

    return scaled_texture_is_ready(command->scaled_texture);
}

static ScaleState scale_state_new(const RenderCommand* command) {
    return (ScaleState) {
        .scaled = command->scaled_texture,
        .tint = splat_color(command->corner_colors),
        .global_alpha = i16x8_new_splat((i16)((command->global_alpha * 0x7FFF) / 255)),
        .channel_count = command->image->channels,
        .fade = command->global_alpha < 255,
    };
}

// Maps the destination rows of the command onto the source image.
typedef struct ScaleMapping {
    float y_scale;
    float image_screen_y;
    float tile_screen_y;
    int dest_x_start;
} ScaleMapping;

static ScaleMapping scale_mapping_new(const RenderCommand* command, const TileInfo* tile_info, const TileClip* clip) {
    const f32x4 aabb = command->aabb;
    // Unpadded height, not padded_height, for the correct scaling ratio
    const float dest_height = f32x4_extract(aabb, 3) - f32x4_extract(aabb, 1);

    ScaleMapping mapping;
    mapping.y_scale = (float)command->image->height / dest_height;
    mapping.image_screen_y = f32x4_extract(aabb, 1);
    mapping.tile_screen_y = f32x4_extract(tile_info->offsets, 1);

    // dest_x_start is the destination pixel we start reading from, relative to the image's left edge in
    // screen coordinates. Computed in floating point to preserve precision with fractional positions.
    const float screen_x_f = f32x4_extract(tile_info->offsets, 0) + (float)clip->x0;
    mapping.dest_x_start
        = clamp((i32)floorf(screen_x_f - f32x4_extract(aabb, 0)), 0, command->scaled_texture->dst_width - 1);
    return mapping;
}

static ScaleSourceRow scale_source_row(const ImageData* image, const ScaleMapping* mapping, const TileClip* clip,
                                       int y) {
    const float screen_y = mapping->tile_screen_y + (float)(clip->y0 + y);
    const int src_y = (int)((screen_y - mapping->image_screen_y) * mapping->y_scale);
    const int texture_stride = image->padded_width;
    const int plane_size = texture_stride * (int)image->padded_height;

    ScaleSourceRow src;
    for_count(c, image->channels) {
        src.plane_lines[c] = image->data + c * plane_size + src_y * texture_stride;
    }
    return src;
}

typedef struct ScaleJob {
    const RenderCommand* command;
    TileTarget target;
    TileClip clip;
    ScaleState state;
    ScaleMapping mapping;
} ScaleJob;

static void scale_render_row(const ScaleJob* job, int y) {
    const ScaleSourceRow src = scale_source_row(job->command->image, &job->mapping, &job->clip, y);
    PlaneRow row = plane_row_new(job->target.output + y * TILE_STRIDE, job->clip.write_offset);
    int dest_x_pos = job->mapping.dest_x_start;

    for_count(x, (job->clip.xlen >> 3)) {
        store_rgb(scale_8_pixels(&job->state, &src, dest_x_pos, &row), &row);
        dest_x_pos += 8;
        row.line_output += 8;
    }

    if ((job->clip.xlen & 0x7) > 0) {
        // Sample the remaining (< 8) destination pixels; the write mask keeps only the valid lanes.
        const i16x8 write_mask = i16x8_write_mask(job->clip.xlen & 0x7);
        store_rgb_masked(scale_8_pixels(&job->state, &src, dest_x_pos, &row), write_mask, &row);
    }
}

void sr_render_image_scale(i16* output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile_info) {
    if (!scale_command_is_valid(command)) {
        return;
    }

    ScaleJob job = { .command = command, .target = { output, scissor_rect, tile_info } };
    if (!tile_clip_setup(command->aabb, &job.target, &job.clip)) {
        return;
    }

    job.state = scale_state_new(command);
    job.mapping = scale_mapping_new(command, tile_info, &job.clip);

    for_count(y, job.clip.ylen) {
        scale_render_row(&job, y);
    }
}
